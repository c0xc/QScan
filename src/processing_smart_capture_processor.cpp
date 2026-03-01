/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (p@c0xc.net)
** This file is part of QScan.
**
** QScan is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** QScan is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with QScan. If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

#include <algorithm>

#ifdef HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <QtMath>

#include "processing/smart_capture_processor.hpp"

SmartCaptureProcessor::SmartCaptureProcessor()
{
}

DetectedRegion
SmartCaptureProcessor::detectDocument(const QImage &input)
{
#ifdef HAVE_OPENCV
    DetectedRegion out;
    if (input.isNull() || input.width() <= 0 || input.height() <= 0)
        return out;

    auto qimageToRgbaMat = [](const QImage &img, QImage *backing) -> cv::Mat
    {
        //Keep a Qt-owned backing image alive while the Mat views its data
        *backing = img.convertToFormat(QImage::Format_RGBA8888);
        return cv::Mat(backing->height(),
                       backing->width(),
                       CV_8UC4,
                       const_cast<uchar *>(backing->constBits()),
                       (size_t)backing->bytesPerLine());
    };

    auto clamp01 = [](double v) -> double
    {
        if (v < 0.0)
            return 0.0;
        if (v > 1.0)
            return 1.0;
        return v;
    };

    auto orderQuad = [](const std::vector<cv::Point2f> &pts) -> std::vector<cv::Point2f>
    {
        //Order corners TL, TR, BR, BL
        //Uses sum/diff heuristic; assumes a reasonably convex quad
        std::vector<cv::Point2f> out_pts(4);

        double best_sum_min = 1e100;
        double best_sum_max = -1e100;
        double best_diff_min = 1e100;
        double best_diff_max = -1e100;

        int idx_sum_min = 0;
        int idx_sum_max = 0;
        int idx_diff_min = 0;
        int idx_diff_max = 0;

        for (int i = 0; i < 4; ++i)
        {
            const double s = pts[i].x + pts[i].y;
            const double d = pts[i].x - pts[i].y;
            if (s < best_sum_min)
            {
                best_sum_min = s;
                idx_sum_min = i;
            }
            if (s > best_sum_max)
            {
                best_sum_max = s;
                idx_sum_max = i;
            }
            if (d < best_diff_min)
            {
                best_diff_min = d;
                idx_diff_min = i;
            }
            if (d > best_diff_max)
            {
                best_diff_max = d;
                idx_diff_max = i;
            }
        }

        out_pts[0] = pts[idx_sum_min];
        out_pts[2] = pts[idx_sum_max];
        out_pts[1] = pts[idx_diff_max];
        out_pts[3] = pts[idx_diff_min];
        return out_pts;
    };

    QImage backing;
    const cv::Mat rgba = qimageToRgbaMat(input, &backing);

    cv::Mat gray;
    cv::cvtColor(rgba, gray, cv::COLOR_RGBA2GRAY);

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);

    cv::Mat edges;
    cv::Canny(blurred, edges, 50.0, 150.0);

    //Close small gaps
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::dilate(edges, edges, kernel, cv::Point(-1, -1), 1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    const double img_area = (double)input.width() * (double)input.height();
    const double min_area = img_area * 0.05;

    double best_score = -1.0;
    std::vector<cv::Point2f> best_quad;

    for (const auto &c : contours)
    {
        const double area = cv::contourArea(c);
        if (area < min_area)
            continue;

        const double peri = cv::arcLength(c, true);
        std::vector<cv::Point> approx;
        cv::approxPolyDP(c, approx, 0.02 * peri, true);
        if ((int)approx.size() != 4)
            continue;

        if (!cv::isContourConvex(approx))
            continue;

        std::vector<cv::Point2f> pts;
        pts.reserve(4);
        for (const cv::Point &p : approx)
            pts.push_back(cv::Point2f((float)p.x, (float)p.y));

        pts = orderQuad(pts);

        //Score: prefer large quads, slightly prefer near-rectangular shapes
        const cv::Rect br = cv::boundingRect(approx);
        const double rect_area = (double)br.width * (double)br.height;
        const double fill = rect_area > 0.0 ? (area / rect_area) : 0.0;

        const double area_ratio = area / img_area;
        const double score = area_ratio * (0.75 + 0.25 * clamp01(fill));
        if (score > best_score)
        {
            best_score = score;
            best_quad = pts;
        }
    }

    if (best_quad.size() != 4)
        return out;

    out.corners.clear();
    out.corners << QPointF(best_quad[0].x, best_quad[0].y)
                << QPointF(best_quad[1].x, best_quad[1].y)
                << QPointF(best_quad[2].x, best_quad[2].y)
                << QPointF(best_quad[3].x, best_quad[3].y);

    //Confidence is currently a monotonic function of quad area vs image
    //For UI usage, callers should still treat detection as best-effort
    out.confidence = clamp01(best_score * 2.0);
    return out;
#else
    //OpenCV not available
    DetectedRegion region;
    region.confidence = 0.0;
    return region;
#endif
}

QImage
SmartCaptureProcessor::extractDocument(const QImage &input, const DetectedRegion &region)
{
#ifdef HAVE_OPENCV
    if (input.isNull())
        return QImage();

    if (!region.isValid())
        return input.copy();

    if (region.corners.size() != 4)
        return input.copy();

    auto qimageToRgbaMat = [](const QImage &img, QImage *backing) -> cv::Mat
    {
        *backing = img.convertToFormat(QImage::Format_RGBA8888);
        return cv::Mat(backing->height(),
                       backing->width(),
                       CV_8UC4,
                       const_cast<uchar *>(backing->constBits()),
                       (size_t)backing->bytesPerLine());
    };

    auto matRgbaToQImageCopy = [](const cv::Mat &m) -> QImage
    {
        if (m.empty())
            return QImage();
        if (m.type() != CV_8UC4)
            return QImage();
        QImage img(m.data, m.cols, m.rows, (int)m.step, QImage::Format_RGBA8888);
        return img.copy();
    };

    auto orderQuad = [](const std::vector<cv::Point2f> &pts) -> std::vector<cv::Point2f>
    {
        std::vector<cv::Point2f> out_pts(4);

        double best_sum_min = 1e100;
        double best_sum_max = -1e100;
        double best_diff_min = 1e100;
        double best_diff_max = -1e100;

        int idx_sum_min = 0;
        int idx_sum_max = 0;
        int idx_diff_min = 0;
        int idx_diff_max = 0;

        for (int i = 0; i < 4; ++i)
        {
            const double s = pts[i].x + pts[i].y;
            const double d = pts[i].x - pts[i].y;
            if (s < best_sum_min)
            {
                best_sum_min = s;
                idx_sum_min = i;
            }
            if (s > best_sum_max)
            {
                best_sum_max = s;
                idx_sum_max = i;
            }
            if (d < best_diff_min)
            {
                best_diff_min = d;
                idx_diff_min = i;
            }
            if (d > best_diff_max)
            {
                best_diff_max = d;
                idx_diff_max = i;
            }
        }

        out_pts[0] = pts[idx_sum_min];
        out_pts[2] = pts[idx_sum_max];
        out_pts[1] = pts[idx_diff_max];
        out_pts[3] = pts[idx_diff_min];
        return out_pts;
    };

    auto dist = [](const cv::Point2f &a, const cv::Point2f &b) -> double
    {
        const double dx = (double)a.x - (double)b.x;
        const double dy = (double)a.y - (double)b.y;
        return qSqrt(dx * dx + dy * dy);
    };

    std::vector<cv::Point2f> src;
    src.reserve(4);
    for (int i = 0; i < 4; ++i)
        src.push_back(cv::Point2f((float)region.corners[i].x(), (float)region.corners[i].y()));
    src = orderQuad(src);

    const double widthA = dist(src[2], src[3]);
    const double widthB = dist(src[1], src[0]);
    const double heightA = dist(src[1], src[2]);
    const double heightB = dist(src[0], src[3]);
    const int out_w = (int)qRound(qMax(widthA, widthB));
    const int out_h = (int)qRound(qMax(heightA, heightB));
    if (out_w <= 1 || out_h <= 1)
        return input.copy();

    std::vector<cv::Point2f> dst;
    dst.reserve(4);
    dst.push_back(cv::Point2f(0.0f, 0.0f));
    dst.push_back(cv::Point2f((float)(out_w - 1), 0.0f));
    dst.push_back(cv::Point2f((float)(out_w - 1), (float)(out_h - 1)));
    dst.push_back(cv::Point2f(0.0f, (float)(out_h - 1)));

    QImage backing;
    const cv::Mat rgba = qimageToRgbaMat(input, &backing);

    const cv::Mat M = cv::getPerspectiveTransform(src, dst);
    cv::Mat warped;
    cv::warpPerspective(rgba, warped, M, cv::Size(out_w, out_h), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    return matRgbaToQImageCopy(warped);
#else
    //OpenCV not available - return simple bounding box crop
    if (region.isValid()) {
        QRectF bounds = region.corners.boundingRect();
        return input.copy(bounds.toRect());
    }
    return input.copy();
#endif
}

bool
SmartCaptureProcessor::isAvailable() const
{
#ifdef HAVE_OPENCV
    return true;
#else
    return false;
#endif
}

QImage
SmartCaptureProcessor::process(const QImage &input)
{
    //Convenience: detect + extract in one step
    DetectedRegion region = detectDocument(input);
    if (region.isValid()) {
        return extractDocument(input, region);
    }
    return input.copy();
}

QString
SmartCaptureProcessor::name() const
{
    return "Smart Capture";
}
