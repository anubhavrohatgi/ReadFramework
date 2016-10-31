/*******************************************************************************************************
 ReadFramework is the basis for modules developed at CVL/TU Wien for the EU project READ. 
  
 Copyright (C) 2016 Markus Diem <diem@caa.tuwien.ac.at>
 Copyright (C) 2016 Stefan Fiel <fiel@caa.tuwien.ac.at>
 Copyright (C) 2016 Florian Kleber <kleber@caa.tuwien.ac.at>

 This file is part of ReadFramework.

 ReadFramework is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ReadFramework is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 The READ project  has  received  funding  from  the European  Union’s  Horizon  2020  
 research  and innovation programme under grant agreement No 674943
 
 related links:
 [1] http://www.caa.tuwien.ac.at/cvl/
 [2] https://transkribus.eu/Transkribus/
 [3] https://github.com/TUWien/
 [4] http://nomacs.org
 *******************************************************************************************************/

#include "Pixel.h"

#include "ImageProcessor.h"
#include "Drawer.h"
#include "Shapes.h"
#include "Utils.h"
#include "Image.h"
#include "Algorithms.h"
#include "Elements.h"

#pragma warning(push, 0)	// no warnings from includes
#include <QColor>
#include <QDebug>
#include <QPainter>

#include <opencv2/imgproc/imgproc.hpp>
#pragma warning(pop)

namespace rdf {


// MserBlob --------------------------------------------------------------------
MserBlob::MserBlob(const std::vector<cv::Point>& pts, 
		const Rect & bbox, 
		const QString& id) : BaseElement(id) {

	mPts = pts;
	mBBox = bbox;
	mCenter = bbox.center();	// cache center
}

bool MserBlob::isNull() const {
	return mPts.empty();
}

double MserBlob::area() const {

	return (double)mPts.size();
}

//double MserBlob::uniqueArea(const QVector<MserBlob>& blobs) const {
//
//	if (!mPts)
//		return 0;
//
//	cv::Mat m(mBBox.size().toCvSize(), CV_8UC1);
//	IP::draw(*relativePts(bbox().topLeft()), m, 1);
//
//	for (const MserBlob& b : blobs) {
//
//		// remove pixels
//		if (bbox().contains(b.bbox())) {
//			IP::draw(*b.relativePts(bbox().topLeft()), m, 0);
//		}
//	}
//
//	return cv::sum(m)[0];
//}

Vector2D MserBlob::center() const {
	return mCenter;
}

Rect MserBlob::bbox() const {
	// TODO: if needed compute it here
	return mBBox;
}

std::vector<cv::Point> MserBlob::pts() const {
	return mPts;
}

std::vector<cv::Point> MserBlob::relativePts(const Vector2D & origin) const {

	std::vector<cv::Point> rPts;

	for (const cv::Point& p : mPts) {

		Vector2D rp = p - origin;
		rPts.push_back(rp.toCvPoint());
	}

	return rPts;
}

cv::Mat MserBlob::toBinaryMask() const {

	cv::Mat img(bbox().size().toCvSize(), CV_8UC1, cv::Scalar(0));
	IP::draw(relativePts(bbox().topLeft()), img, 1);

	return img;
}

QSharedPointer<Pixel> MserBlob::toPixel() const {

	Ellipse e = Ellipse::fromData(mPts);
	QSharedPointer<Pixel> p(new Pixel(e, bbox(), id()));	// assign my ID to pixel - so we can trace back that they are related

	return p;
}

double MserBlob::overlapArea(const Rect& r) const {

	if (!mBBox.contains(r))
		return 0.0;

	Vector2D tl = Vector2D::max(mBBox.topLeft(), r.topLeft());
	Vector2D br = Vector2D::min(mBBox.bottomRight(), r.bottomRight());

	double width = br.x() - tl.x();
	double height = br.y() - tl.y();

	assert(width > 0 && height > 0);

	return width * height;
}

void MserBlob::draw(QPainter & p) {

	QColor col = Drawer::instance().pen().color();
	col.setAlpha(60);
	Drawer::instance().setColor(col);
	Drawer::instance().drawPoints(p, mPts);

	// draw bounding box
	col.setAlpha(255);
	Drawer::instance().setStrokeWidth(1);
	Drawer::instance().setColor(col);
	Drawer::instance().drawRect(p, bbox().toQRectF());

	// draw center
	//Drawer::instance().setStrokeWidth(3);
	Drawer::instance().drawPoint(p, bbox().center().toQPointF());
}

// PixelStats --------------------------------------------------------------------
PixelStats::PixelStats(const cv::Mat& orHist, 
	const cv::Mat& sparsity, 
	double scale, 
	const QString& id) : BaseElement(id) {

	mScale = scale;
	convertData(orHist, sparsity);

}

void PixelStats::convertData(const cv::Mat& orHist, const cv::Mat& sparsity) {

	double lambda = 0.5;	// weights sparsity & line frequency measure (1.0 is dft only)
	assert(orHist.rows == sparsity.cols);

	// enrich our data
	mData = cv::Mat(idx_end, orHist.rows, CV_32FC1);
	sparsity.copyTo(mData.row(sparsity_idx));

	float* maxP = mData.ptr<float>(max_val_idx);
	float* spP = mData.ptr<float>(sparsity_idx);
	float* tlP = mData.ptr<float>(spacing_idx);
	float* cbP = mData.ptr<float>(combined_idx);

	for (int rIdx = 0; rIdx < orHist.rows; rIdx++) {

		double minVal = 0;
		cv::Point minIdx;
		cv::minMaxLoc(orHist.row(rIdx), &minVal, 0, &minIdx);

		maxP[rIdx] = (float)minVal;
		tlP[rIdx] = (float)minIdx.x;
		cbP[rIdx] = (float)(minVal*lambda + (1.0 - lambda) * spP[rIdx]);
	}

	// find dominant peak
	cv::Point minIdx;
	cv::minMaxLoc(mData.row(combined_idx), &mMinVal, 0, &minIdx);

	mOrIdx = minIdx.x;
	mHistSize = orHist.cols;
}

bool PixelStats::isEmpty() const {
	return mData.empty();
}

void PixelStats::setOrientationIndex(int orIdx) {

	assert(orIdx >= 0 && orIdx < mData.cols);
	mOrIdx = orIdx;
}

int PixelStats::orientationIndex() const {

	return mOrIdx;
}

double PixelStats::orientation() const {

	return mOrIdx * CV_PI / numOrientations();
}

/// <summary>
/// Returns the orientation vector scaled to unit length.
/// </summary>
/// <returns>The orientation vector (0° = postive y-axis).</returns>
Vector2D PixelStats::orVec() const {
	
	Vector2D vec(1, 0);
	vec.rotate(orientation());

	return vec;
}

int PixelStats::lineSpacingIndex() const {

	if (mData.rows < spacing_idx || mOrIdx < 0 || mOrIdx >= mData.cols)
		return 0;

	return qRound(mData.ptr<float>(spacing_idx)[mOrIdx]);
}

double PixelStats::lineSpacing() const {

	// * 2.0 -> radius vs. diameter
	return (double)lineSpacingIndex()/mHistSize * mHistSize/scale() * scale() * 2.0;
}

int PixelStats::numOrientations() const {

	return mData.cols;
}

double PixelStats::minVal() const {
	
	return mMinVal;
}

double PixelStats::scale() const {
	
	return mScale;
}

cv::Mat PixelStats::data(const DataIndex& dIdx) {

	if (dIdx == all_data)
		return mData;

	assert(dIdx >= 0 && dIdx <= mData.cols);
	return mData.row(dIdx);
}

// Pixel --------------------------------------------------------------------
Pixel::Pixel() {

}

Pixel::Pixel(const Ellipse & ellipse, const Rect& bbox, const QString& id) : BaseElement(id) {

	mIsNull = false;
	mEllipse = ellipse;
	mBBox = bbox;
}

bool Pixel::isNull() const {
	return mIsNull;
}

//Vector2D Pixel::center() const {
//	return mEllipse.center();
//}

Vector2D Pixel::size() const {
	return mEllipse.axis();
}

double Pixel::angle() const {
	return mEllipse.angle();
}

Ellipse Pixel::ellipse() const {
	return mEllipse;
}

//Rect Pixel::bbox() const {
//	return mBBox;
//}


void Pixel::addStats(const QSharedPointer<PixelStats>& stats) {
	mStats << stats;
}

QSharedPointer<PixelStats> Pixel::stats(int idx) const {
	
	// select best scale by default
	if (idx == -1) {

		QSharedPointer<PixelStats> bps;
		double minScaleV = DBL_MAX;

		for (auto ps : mStats) {
			
			if (minScaleV > ps->minVal()) {
				bps = ps;
				minScaleV = ps->minVal();
			}
		}

		return bps;
	}

	if (idx < 0 || idx >= mStats.size()) {
		qWarning() << "cannot return PixelStats at" << idx;
		return QSharedPointer<PixelStats>();
	}
	
	return mStats[idx];
}

void Pixel::setTabStop(const PixelTabStop& tabStop) {
	mTabStop = tabStop;
}

/// <summary>
/// Returns tab stop statistics or NULL if tab stops are not computed.
/// PixelTabStop stores if the Pixel is a tab stop (left/right) candidate or not.
/// </summary>
/// <returns>Tab stop statistics.</returns>
PixelTabStop Pixel::tabStop() const {
	return mTabStop;
}

void Pixel::draw(QPainter & p, double alpha, const DrawFlag & df) const {
	
	if (df == draw_all) {
		QPen pen = p.pen();
		p.setPen(QColor(255, 33, 33));
		p.drawText(center().toQPoint(), id());
		p.setPen(pen);
	}

	if (stats() && (df != draw_ellipse_only)) {

		auto s = stats();

		//QColor c(255,33,33);

		//if (s->scale() == 256)
		//	c = ColorManager::colors()[0];
		//else if (s->scale() == 128)
		//	c = ColorManager::colors()[1];
		//else if (s->scale() == 64)
		//	c = ColorManager::colors()[2];

		//QPen pen = p.pen();
		//p.setPen(c);

		Vector2D vec = stats()->orVec();
		vec *= stats()->lineSpacing();
		vec = vec + center();

		p.drawLine(Line(center(), vec).line());
		//p.setPen(pen);
	}

	if (stats() && tabStop().type() != PixelTabStop::type_none) {

		// get tab vec
		Vector2D vec = stats()->orVec();
		vec *= 40;
		vec.rotate(tabStop().orientation());

		QPen oPen = p.pen();
		p.setPen(ColorManager::red());
		p.drawLine(Line(center(), center()+vec).line());
		p.setPen(oPen);
	}

	//if (stats()) {
	//	QPen pe = p.pen();
	//	p.setPen(QColor(255, 0, 0));
	//	Vector2D upper = mEllipse.getPoint(stats()->orientation());
	//	Vector2D lower = mEllipse.getPoint(stats()->orientation() + CV_PI);
	//	p.drawLine(upper.toQPointF(), lower.toQPointF());
	//	p.setPen(pe);
	//}

	if (!stats() || df != draw_stats_only)
		mEllipse.draw(p, alpha);

}

PixelEdge::PixelEdge() {
}

// PixelEdge --------------------------------------------------------------------
PixelEdge::PixelEdge(const QSharedPointer<Pixel> first, 
	const QSharedPointer<Pixel> second, 
	const QString & id) : 
	BaseElement(id) {

	mIsNull = false;
	mFirst = first;
	mSecond = second;

	// cache edge
	if (mFirst && mSecond)
		mEdge = Line(mFirst->center(), mSecond->center());

}

bool PixelEdge::isNull() const {
	return mIsNull;
}

double PixelEdge::edgeWeight() const {

	if (!mFirst || !mSecond)
		return 0.0;

	double beta = 1.0;

	if (mFirst->stats() && mSecond->stats()) {
	
		double sp = mFirst->stats()->lineSpacing();
		double sq = mSecond->stats()->lineSpacing();
		double nl = (beta * edge().squaredLength()) / (sp * sp + sq * sq);
		double ew = 1.0-exp(-nl);

		if (ew < 0.0 || ew > 1.0) {
			qDebug() << "illegal edge weight: " << ew;
		}
		//else
		//	qDebug() << "weight: " << nl;

		// TODO: add mu(fp,fq) according to koo's indices
		return ew;
	}

	qDebug() << "no stats when computing the scaled edges...";

	return 0.0;

	//// get minimum scale
	//double ms = qMin(mFirst->ellipse().majorAxis(), mSecond->ellipse().majorAxis());
	//assert(ms > 0);

	//return edge().length() / ms;
}

Line PixelEdge::edge() const {

	return mEdge;
}

QSharedPointer<Pixel> PixelEdge::first() const {
	
	return mFirst;
}

QSharedPointer<Pixel> PixelEdge::second() const {

	return mSecond;
}

void PixelEdge::draw(QPainter & p) const {

	edge().draw(p);
}

// LineEdge --------------------------------------------------------------------
LineEdge::LineEdge() {
}

LineEdge::LineEdge(const PixelEdge & pe) : PixelEdge(pe) {
	mStatsWeight = calcWeight();
}

LineEdge::LineEdge(
	const QSharedPointer<Pixel> first, 
	const QSharedPointer<Pixel> second, 
	const QString & id) : 
	PixelEdge(first, second, id) {

	mStatsWeight = calcWeight();
}

double LineEdge::edgeWeight() const {
	return mStatsWeight;
}

double LineEdge::calcWeight() const {
	double d1 = statsWeight(mFirst);
	double d2 = statsWeight(mSecond);

	return qMax(abs(d1), abs(d2));
}

double LineEdge::statsWeight(const QSharedPointer<Pixel>& pixel) const {

	if (!pixel)
		return DBL_MAX;

	if (!pixel->stats()) {
		qWarning() << "pixel stats are NULL where they must not be...";
		return DBL_MAX;
	}

	Vector2D vec = pixel->stats()->orVec();
	Vector2D eVec = edge().vector();

	return vec * eVec;
}

}

double rdf::PixelDistance::euclidean(const QSharedPointer<const Pixel>& px1, const QSharedPointer<const Pixel>& px2) {
	
	assert(!px1.isNull() && !px2.isNull());
	return Vector2D(px2->center() - px1->center()).length();
}

double rdf::PixelDistance::angleWeighted(const QSharedPointer<const Pixel>& px1, const QSharedPointer<const Pixel>& px2) {

	assert(!px1.isNull() && !px2.isNull());

	if (!px1->stats() || !px2->stats()) {
		qWarning() << "cannot compute angle weighted distance if stats are NULL";
		return euclidean(px1, px2);
	}

	Vector2D edge = px2->center() - px1->center();
	double dt1 = std::abs(edge.theta(px1->stats()->orVec()));
	double dt2 = std::abs(edge.theta(px2->stats()->orVec()));

	double a = qMin(dt1, dt2);

	return edge.length() * (a + 0.1);	// + 0.1 -> we don't want to map all 'aligned' pixels to 0
}