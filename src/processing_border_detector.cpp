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

#include <QtGlobal>
#include <QVector>
#include <QtMath>

#include "processing/border_detector.hpp"

/*
BorderDetector: scanner-style border trim (paper-in-frame)

Goal
- Detect borders around the scanned object, usually a paper sheet
- Return an axis-aligned QRect in input image coordinates
- Remain fail-safe by default, return full-frame if unsure

Interface contract
- Border detection returns a sub region within the image
- ScannedPage stores full image (currentImage()) and crop/edits
- GUI calls Border Detection module to determine crop rect (+ crop preview)
- In camera mode, GUI calls Warp Detection module to get polygon (+ crop/warp preview)
- The stored crop geometry should be applied later during export via ScannedPage::processedImage()

Input assumptions
- Primary use case: scanner output (flatbed/ADF), paper roughly aligned
- Background around paper often uniform, not guaranteed
- Camera input is a different problem space (needs quad + perspective warp) # TODO WarpDetector

Current approach (Qt-only)
- Downscale for speed on large images
- Estimate background intensity from border samples
- Classify sampled pixels as background vs content by gray deviation threshold
- Scan rows/columns from edges inward until content is detected
- Map bounds back to original resolution
- Apply sanity checks, fall back to full-frame on low confidence

Failure modes
- Low contrast paper vs background
- Border samples contaminated by content, shadows, hands, clips
- Sparse-content pages risk over-tight crops if paper edge is missed

Candidate cycling (experimental)
- On demand, try to detect the next candidate (smaller rect)
- Default to the safest (largest) candidate; keep alternates for cycling
- The GUI can cycle candidates (e.g. repeated Auto clicks)

NOTE rough and lazy adoption of a common algorithm, for experimental use, initial focus is on interface...

*/

BorderDetector::BorderDetector()
{
}

QRect
BorderDetector::detectContentBounds(const QImage &input)
{
    ContentBounds r = detectBorders(input);
    if (!r.best.isValid() || r.best.isNull())
        return input.isNull() ? QRect() : input.rect();
    return r.best;
}

BorderDetector::ContentBounds
BorderDetector::detectBorders(const QImage &input)
{
    ContentBounds out;

    if (input.isNull() || input.width() <= 0 || input.height() <= 0)
        return out;

    const QRect full = input.rect();

    //Step: prepare a smaller working image for fast scanning
    //Large scanner pages can be several thousand pixels wide
    //1200px keeps edge detection fast while preserving enough detail for borders
    const int max_dim = 1200;
    QImage work = input;
    if (qMax(input.width(), input.height()) > max_dim)
    {
        work = input.scaled(max_dim,
                            max_dim,
                            Qt::KeepAspectRatio,
                            Qt::FastTransformation);
        //Qt::FastTransformation is sufficient here
        //We only need coarse gradients and edges, not high-quality resampling
    }

    if (work.isNull() || work.width() <= 0 || work.height() <= 0)
    {
        out.best = full;
        return out;
    }

    if (work.format() != QImage::Format_ARGB32 && work.format() != QImage::Format_RGB32)
        work = work.convertToFormat(QImage::Format_ARGB32);

    const int w = work.width();
    const int h = work.height();

    //Step: estimate background intensity separately for each edge
    //A single global bg value breaks with scanner lighting gradients
    //Example: left border is brighter/darker than right border but looks "uniform" to the eye
    QVector<int> edge_samples_top;
    QVector<int> edge_samples_bottom;
    QVector<int> edge_samples_left;
    QVector<int> edge_samples_right;
    edge_samples_top.reserve(512);
    edge_samples_bottom.reserve(512);
    edge_samples_left.reserve(512);
    edge_samples_right.reserve(512);

    //Step: sample a few hundred pixels per edge for background
    //Target a few hundred samples per edge, independent of resolution
    const int step_x = qMax(1, w / 240);
    const int step_y = qMax(1, h / 240);

    const int y_top = 0;
    const int y_bottom = h - 1;
    const int x_left = 0;
    const int x_right = w - 1;

    const QRgb *top_row = reinterpret_cast<const QRgb *>(work.constScanLine(y_top));
    const QRgb *bottom_row = reinterpret_cast<const QRgb *>(work.constScanLine(y_bottom));
    for (int x = 0; x < w; x += step_x)
    {
        edge_samples_top.push_back(qGray(top_row[x]));
        edge_samples_bottom.push_back(qGray(bottom_row[x]));
    }

    for (int y = 0; y < h; y += step_y)
    {
        const QRgb *row = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        edge_samples_left.push_back(qGray(row[x_left]));
        edge_samples_right.push_back(qGray(row[x_right]));
    }

    auto medianOr = [&](QVector<int> &samples, int fallback) -> int
    {
        if (samples.isEmpty())
            return fallback;
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };

    //Median is robust against a few "contaminated" samples (hands, clips, page content)
    const int bg_top = medianOr(edge_samples_top, 255);
    const int bg_bottom = medianOr(edge_samples_bottom, 255);
    const int bg_left = medianOr(edge_samples_left, 255);
    const int bg_right = medianOr(edge_samples_right, 255);

    //Step: choose sampling stride for content detection
    //Roughly ~600 samples per row/col keeps it fast while resisting noise
    const int stride_x = qMax(1, w / 600);
    const int stride_y = qMax(1, h / 600);

    auto detectForDelta = [&](int delta) -> QRect
    {
        //"delta" is the gray-distance threshold from background
        //Lower delta => more sensitive => more pixels count as content
        auto isContentGray = [&](int gray, int bg) -> bool
        {
            return qAbs(gray - bg) > delta;
        };

        //Step: row/col content heuristics
        //Use edge-specific background so one bright/dark side doesn't dominate the decision
        auto rowHasContent = [&](int y, int bg) -> bool
        {
            const QRgb *row = reinterpret_cast<const QRgb *>(work.constScanLine(y));
            int content = 0;
            int samples = 0;
            for (int x = 0; x < w; x += stride_x)
            {
                samples++;
                if (isContentGray(qGray(row[x]), bg))
                    content++;
            }
            if (samples <= 0)
                return true;

            const qreal ratio = (qreal)content / (qreal)samples;
            //content>=6 avoids single speck/noise triggering content
            //ratio>=0.005 means at least ~0.5% of sampled pixels look non-background
            return (content >= 6) && (ratio >= 0.005);
        };

        auto colHasContent = [&](int x, int bg) -> bool
        {
            int content = 0;
            int samples = 0;
            for (int y = 0; y < h; y += stride_y)
            {
                const QRgb *row = reinterpret_cast<const QRgb *>(work.constScanLine(y));
                samples++;
                if (isContentGray(qGray(row[x]), bg))
                    content++;
            }
            if (samples <= 0)
                return true;

            const qreal ratio = (qreal)content / (qreal)samples;
            //Same thresholds as rowHasContent
            return (content >= 6) && (ratio >= 0.005);
        };

        //Step: scan inward from edges
        //Require a short run of consecutive content rows/cols before accepting an edge
        //This filters thin shadows/lines at the extreme edges that would otherwise pin the crop
        const int min_run = 3;
        //min_run filters single-row/col noise and thin edge shadows
        //3 is a small compromise that still catches faint paper edges

        bool found_top = false;
        int top = 0;
        int run = 0;
        for (int y = 0; y < h; y++)
        {
            if (rowHasContent(y, bg_top))
            {
                run++;
                if (run >= min_run)
                {
                    top = y - run + 1;
                    found_top = true;
                    break;
                }
            }
            else
            {
                run = 0;
            }
        }

        bool found_bottom = false;
        int bottom = h - 1;
        run = 0;
        for (int y = h - 1; y >= 0; y--)
        {
            if (rowHasContent(y, bg_bottom))
            {
                run++;
                if (run >= min_run)
                {
                    bottom = y + run - 1;
                    found_bottom = true;
                    break;
                }
            }
            else
            {
                run = 0;
            }
        }

        bool found_left = false;
        int left = 0;
        run = 0;
        for (int x = 0; x < w; x++)
        {
            if (colHasContent(x, bg_left))
            {
                run++;
                if (run >= min_run)
                {
                    left = x - run + 1;
                    found_left = true;
                    break;
                }
            }
            else
            {
                run = 0;
            }
        }

        bool found_right = false;
        int right = w - 1;
        run = 0;
        for (int x = w - 1; x >= 0; x--)
        {
            if (colHasContent(x, bg_right))
            {
                run++;
                if (run >= min_run)
                {
                    right = x + run - 1;
                    found_right = true;
                    break;
                }
            }
            else
            {
                run = 0;
            }
        }

        if (!found_top || !found_bottom || !found_left || !found_right)
            return QRect();

        if (right <= left || bottom <= top)
            return QRect();

        //Expand by 1 pixel to avoid cutting too tight
        left = qMax(0, left - 1);
        top = qMax(0, top - 1);
        right = qMin(w - 1, right + 1);
        bottom = qMin(h - 1, bottom + 1);

        QRect work_rect(QPoint(left, top), QPoint(right, bottom));
        if (!work_rect.isValid() || work_rect.isNull())
            return QRect();

        //Step: map work image bounds back to original coordinates
        const qreal sx = (qreal)input.width() / (qreal)w;
        const qreal sy = (qreal)input.height() / (qreal)h;

        QRect mapped;
        mapped.setX((int)qFloor(work_rect.x() * sx));
        mapped.setY((int)qFloor(work_rect.y() * sy));
        mapped.setWidth((int)qCeil(work_rect.width() * sx));
        mapped.setHeight((int)qCeil(work_rect.height() * sy));
        mapped = mapped.intersected(full);

        if (!mapped.isValid() || mapped.isNull())
            return QRect();

        const int trim_l = mapped.left();
        const int trim_t = mapped.top();
        const int trim_r = full.right() - mapped.right();
        const int trim_b = full.bottom() - mapped.bottom();
        //Ignore micro-crops from resampling/rounding
        //This avoids annoying 1-2px "jitter" where the UI crop changes but the image looks the same
        if (trim_l <= 2 && trim_t <= 2 && trim_r <= 2 && trim_b <= 2)
            return QRect();

        //Fail-safe never crop to a tiny region
        //At least 10% of the original image and at least 32px in each direction
        if (mapped.width() < qMax(32, input.width() / 10) || mapped.height() < qMax(32, input.height() / 10))
            return QRect();

        return mapped;
    };

    //Step: generate a candidate set by refining delta where the rect actually changes
    //Fixed delta lists often produce near-duplicates (crop "jitter"), so we dedupe and refine adaptively
    const qreal eps = 0.002;
    //eps is a normalized geometry tolerance that matches the GUI candidate dedupe

    auto approxEqual = [&](const QRect &a, const QRect &b) -> bool
    {
        if (a == b)
            return true;
        if (!a.isValid() || a.isNull() || !b.isValid() || b.isNull())
            return false;

        const qreal ax = (qreal)a.x() / (qreal)input.width();
        const qreal ay = (qreal)a.y() / (qreal)input.height();
        const qreal aw = (qreal)a.width() / (qreal)input.width();
        const qreal ah = (qreal)a.height() / (qreal)input.height();

        const qreal bx = (qreal)b.x() / (qreal)input.width();
        const qreal by = (qreal)b.y() / (qreal)input.height();
        const qreal bw = (qreal)b.width() / (qreal)input.width();
        const qreal bh = (qreal)b.height() / (qreal)input.height();

        return qAbs(ax - bx) <= eps
            && qAbs(ay - by) <= eps
            && qAbs(aw - bw) <= eps
            && qAbs(ah - bh) <= eps;
    };

    auto addUnique = [&](QVector<QRect> &out_rects, const QRect &cand) -> bool
    {
        if (!cand.isValid() || cand.isNull() || cand == full)
            return false;
        for (const QRect &u : out_rects)
        {
            if (approxEqual(u, cand))
                return false;
        }
        out_rects.push_back(cand);
        return true;
    };

    //Base deltas provide anchor points; refinement fills in only where it yields new, distinct rectangles
    const int base_deltas[] = { 22, 18, 14, 10 };
    //Higher delta is more conservative (needs stronger contrast), lower delta is more sensitive

    struct DeltaRect
    {
        int delta;
        QRect rect;
    };

    QVector<DeltaRect> anchors;
    anchors.reserve((int)(sizeof(base_deltas) / sizeof(base_deltas[0])));

    bool have_confident_level = false;
    QVector<QRect> unique;
    unique.reserve(8);

    for (int i = 0; i < (int)(sizeof(base_deltas) / sizeof(base_deltas[0])); i++)
    {
        const int d = base_deltas[i];
        const QRect cand = detectForDelta(d);
        if (!cand.isValid() || cand.isNull() || cand == full)
            continue;

        anchors.push_back({ d, cand });
        addUnique(unique, cand);

        //Conservative confidence: require a stricter (less sensitive) delta to succeed
        if (d >= 18)
            have_confident_level = true;
    }

    std::sort(anchors.begin(), anchors.end(), [](const DeltaRect &a, const DeltaRect &b)
    {
        return a.delta > b.delta;
    });

    struct DeltaRange
    {
        int low;
        int high;
        QRect rect_low;
        QRect rect_high;
    };

    QVector<DeltaRange> ranges;
    ranges.reserve(8);
    for (int i = 0; i + 1 < anchors.size(); i++)
    {
        const DeltaRect &hi = anchors[i];
        const DeltaRect &lo = anchors[i + 1];
        if (hi.delta - lo.delta <= 1)
            continue;
        if (approxEqual(hi.rect, lo.rect))
            continue;
        ranges.push_back({ lo.delta, hi.delta, lo.rect, hi.rect });
    }

    //Cap candidates so cycling stays predictable and detection stays fast
    const int max_candidates = 8;

    while (!ranges.isEmpty() && unique.size() < max_candidates)
    {
        const DeltaRange r = ranges.takeLast();
        if (r.high - r.low <= 1)
            continue;

        const int mid = (r.low + r.high) / 2;
        if (mid <= r.low || mid >= r.high)
            continue;

        const QRect cand = detectForDelta(mid);
        if (!cand.isValid() || cand.isNull() || cand == full)
            continue;

        addUnique(unique, cand);
        if (mid >= 18)
            have_confident_level = true;

        const bool eq_low = approxEqual(cand, r.rect_low);
        const bool eq_high = approxEqual(cand, r.rect_high);
        if (!eq_low && mid - r.low > 1)
            ranges.push_back({ r.low, mid, r.rect_low, cand });
        if (!eq_high && r.high - mid > 1)
            ranges.push_back({ mid, r.high, cand, r.rect_high });
    }

    if (unique.isEmpty())
    {
        out.best = full;
        out.confident = false;
        return out;
    }

    //Sort candidates largest->smallest so "next" cycles towards tighter crops
    std::sort(unique.begin(), unique.end(), [](const QRect &a, const QRect &b)
    {
        const qint64 aa = (qint64)a.width() * (qint64)a.height();
        const qint64 bb = (qint64)b.width() * (qint64)b.height();
        return aa > bb;
    });

    out.best = unique[0];
    out.candidates = unique;
    //Conservative confidence: only claim confidence if a stricter delta level found a valid rect
    out.confident = have_confident_level;
    return out;
}

double
BorderDetector::detectSkewAngle(const QImage &input)
{
    //TODO skew detection
    //Idea: row projection peaks
    return 0.0;
}

QImage
BorderDetector::process(const QImage &input)
{
    //Convenience detect + crop
    QRect bounds = detectContentBounds(input);
    return input.copy(bounds);
}

QString
BorderDetector::name() const
{
    return "Border Detector";
}

bool
BorderDetector::isAvailable() const
{
    return true;  //always available (Qt-only)
}
