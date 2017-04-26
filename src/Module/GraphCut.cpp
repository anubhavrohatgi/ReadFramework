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

#include "GraphCut.h"

// read includes
#include "Image.h"
#include "ImageProcessor.h"

#pragma warning(push, 0)	// no warnings from includes

#include <QPixmap>
#include <QPainter>

//#pragma warning(disable: 4706)
#include "GCGraph.hpp"
#include "graphcut/GCoptimization.h"

#pragma warning(pop)

namespace rdf {

// GraphCutConfig --------------------------------------------------------------------
GraphCutConfig::GraphCutConfig() : ModuleConfig("Graph Cut") {
}

double GraphCutConfig::scaleFactor() const {
	return mScaleFactor;
}

int GraphCutConfig::numIter() const {
	return mGcIter;
}

void GraphCutConfig::load(const QSettings & settings) {

	mGcIter = settings.value("numIter", mGcIter).toInt();
	mScaleFactor = settings.value("scaleFactor", mScaleFactor).toDouble();
}

void GraphCutConfig::save(QSettings & settings) const {
	settings.setValue("numIter", mGcIter);
	settings.setValue("scaleFactor", mScaleFactor);
}

// GraphCutPixel --------------------------------------------------------------------
GraphCutPixel::GraphCutPixel(const PixelSet & set) : mSet(set) {
	mWeightFnc = PixelDistance::orientationWeighted;
	mConnector = QSharedPointer<DelauneyPixelConnector>::create();

	mConfig = QSharedPointer<GraphCutConfig>::create();
}

bool GraphCutPixel::isEmpty() const {
	return mSet.isEmpty();
}

QSharedPointer<GraphCutConfig> GraphCutPixel::config() const {
	return qSharedPointerCast<GraphCutConfig>(mConfig);
}

PixelSet GraphCutPixel::set() const {
	return mSet;
}

QSharedPointer<GCoptimizationGeneralGraph> GraphCutPixel::graphCut(const PixelGraph & graph) const {

	if (graph.isEmpty()) {
		return QSharedPointer<GCoptimizationGeneralGraph>();
	}

	int nLabels = numLabels();

	QVector<QSharedPointer<Pixel> > pixel = graph.set().pixels();

	// get costs and smoothness term
	cv::Mat c = costs(nLabels);				// Set size x #labels
	cv::Mat sm = labelDistMatrix(nLabels);	// #labels x #labels

	// init the graph
	QSharedPointer<GCoptimizationGeneralGraph> gc(new GCoptimizationGeneralGraph(pixel.size(), nLabels));
	gc->setDataCost(c.ptr<int>());
	gc->setSmoothCost(sm.ptr<int>());
	
	// create neighbors
	const QVector<QSharedPointer<PixelEdge> >& edges = graph.edges();
	for (int idx = 0; idx < pixel.size(); idx++) {

		for (int edgeIdx : graph.edgeIndexes(pixel.at(idx)->id())) {

			assert(edgeIdx != -1);

			// get vertex ID
			const QSharedPointer<PixelEdge>& pe = edges[edgeIdx];
			int sVtxIdx = graph.pixelIndex(pe->second()->id());

			// compute weight
			double rawWeight = mWeightFnc(pe.data());
			int w = qRound((1.0 - rawWeight) * config()->scaleFactor());

			gc->setNeighbors(idx, sVtxIdx, w);
		}
	}

	// run the expansion-move
	try {
		//gc->expansion(config()->numIter());
		qDebug() << "energy before:" << gc->compute_energy();
		gc->swap(config()->numIter());
		qDebug() << "energy after:" << gc->compute_energy();
	}
	catch (GCException gce) {

		mWarning << "exception while performing graph-cut";
		mWarning << QString::fromUtf8(gce.message);
		return QSharedPointer<GCoptimizationGeneralGraph>();
	}

	return gc;
}

int GraphCutPixel::numLabels() const {
	return 0;
}


// GraphCutOrientation --------------------------------------------------------------------
GraphCutOrientation::GraphCutOrientation(const PixelSet& set) : GraphCutPixel(set) {
}

bool GraphCutOrientation::checkInput() const {
	return !isEmpty();
}

bool GraphCutOrientation::compute() {

	if (!checkInput())
		return false;

	Timer dt;

	// create graph
	PixelGraph graph(mSet);
	graph.connect(*mConnector);

	// perform graphcut
	auto gc = graphCut(graph);

	if (gc) {
		QVector<QSharedPointer<Pixel> > pixel = graph.set().pixels();
		for (int idx = 0; idx < pixel.size(); idx++) {

			auto ps = pixel[idx]->stats();
			ps->setOrientationIndex(gc->whatLabel(idx));
		}
	}

	mInfo << "computed in" << dt;

	return true;
}

cv::Mat GraphCutOrientation::costs(int numLabels) const {

	// fill costs
	cv::Mat data(mSet.size(), numLabels, CV_32SC1);

	for (int idx = 0; idx < mSet.size(); idx++) {

		auto ps = mSet[idx]->stats();
		assert(ps);

		cv::Mat cData = ps->data(PixelStats::combined_idx);
		cData.convertTo(data.row(idx), CV_32SC1, config()->scaleFactor());	// TODO: check scaling
	}

	return data;
}

cv::Mat GraphCutOrientation::labelDistMatrix(int numLabels) const {

	cv::Mat orDist(numLabels, numLabels, CV_32SC1);

	for (int rIdx = 0; rIdx < orDist.rows; rIdx++) {

		unsigned int* sPtr = orDist.ptr<unsigned int>(rIdx);

		for (int cIdx = 0; cIdx < orDist.cols; cIdx++) {

			// set smoothness cost for orientations
			int diff = abs(rIdx - cIdx);
			sPtr[cIdx] = qMin(diff, numLabels - diff);
		}
	}

	return orDist;
}

/// <summary>
/// Returns the numbers of labels (states).
/// The statistics' columns == the number of possible labels
/// </summary>
/// <returns></returns>
int GraphCutOrientation::numLabels() const {
	return mSet.isEmpty() ? 0 : mSet.pixels()[0]->stats()->data().cols;
}

cv::Mat GraphCutOrientation::draw(const cv::Mat & img) const {

	// debug - remove
	QPixmap pm = Image::mat2QPixmap(img);
	QPainter p(&pm);

	// show the graph
	PixelGraph graph(mSet);
	graph.connect(*mConnector);

	p.setPen(ColorManager::darkGray(0.3));
	graph.draw(p);

	for (auto px : mSet.pixels()) {
		p.setPen(ColorManager::getColor());
		px->draw(p, 0.3, (Pixel::DrawFlag)(Pixel::draw_stats));
	}

	return Image::qPixmap2Mat(pm);
}

// GraphCutTextLine --------------------------------------------------------------------
GraphCutTextLine::GraphCutTextLine(const QVector<PixelSet>& sets) : GraphCutPixel(PixelSet::merge(sets)) {
	mWeightFnc = PixelDistance::euclidean;
	
	mTextLines = sets;
}

bool GraphCutTextLine::compute() {
	
	if (!checkInput())
		return false;

	Timer dt;

	// remove small text lines
	QVector<PixelSet> tlf;
	for (auto tl : mTextLines)
		if (tl.size() > 5)
			tlf << tl;
	mTextLines = tlf;

	PixelGraph graph(mSet);
	graph.connect(*mConnector);
	auto gc = graphCut(graph);

	int ntl = mTextLines.size();

	if (gc) {

		// convert gc labels to text lines
		QMap<int, PixelSet> tlMap;
		auto pixels = mSet.pixels();

		for (int idx = 0; idx < pixels.size(); idx++) {

			int cl = gc->whatLabel(idx);

			// append pixel to text line
			if (tlMap.contains(cl))
				tlMap[cl].add(pixels[idx]);
			else {
				PixelSet ps;
				ps.add(pixels[idx]);
				tlMap.insert(cl, ps);
			}
		}

		mTextLines = tlMap.values().toVector();
	}

	mInfo << "computed in" << dt;
	qDebug() << mTextLines.size() << "/" << ntl << "textlines after graph-cut";

	return true;
}

cv::Mat GraphCutTextLine::draw(const cv::Mat & img) const {
	
	// debug - remove
	QPixmap pm = Image::mat2QPixmap(img);
	QPainter p(&pm);

	// show the graph
	PixelGraph graph(mSet);
	graph.connect(*mConnector);

	p.setPen(ColorManager::darkGray(0.3));
	graph.draw(p);

	p.setOpacity(0.5);

	for (auto tl : mTextLines) {

		p.setPen(ColorManager::darkGray());
		tl.draw(p, PixelSet::draw_poly);
	}

	// draw baselines
	p.setOpacity(1.0);
	p.setPen(ColorManager::pink());

	for (auto tl : mTextLines) {
		tl.fitLine().draw(p);
	}

	//// the next lines show the mahalanobis distance
	//const PixelSet& tl = mTextLines[10];

	//cv::Mat d = mahalanobisDists(tl, pixelSetCentersToMat(mSet));
	//int* dp = d.ptr<int>();

	//for (int idx = 0; idx < d.rows; idx++) {
	//	p.setPen(QColor(qMin(dp[idx]/25, 255), 0, 0));
	//	mSet.pixels()[idx]->draw(p);
	//}

	//// draw currently selected text line
	//p.setOpacity(1.0);
	//p.setPen(ColorManager::pink());
	////tl.fitEllipse().draw(p, 0.1);
	//tl.draw(p, PixelSet::draw_poly);

	//saveDistsDebug("C:/temp/line-dists/line.jpg", img);

	return Image::qPixmap2Mat(pm);
}

QVector<PixelSet> GraphCutTextLine::textLines() {
	return mTextLines;
}

bool GraphCutTextLine::checkInput() const {
	return !isEmpty();
}

cv::Mat GraphCutTextLine::costs(int numLabels) const {

	// fill costs
	cv::Mat data(mSet.size(), numLabels, CV_64FC1);

	//  -------------------------------------------------------------------- compute mahalnobis dists
	// convert centers
	cv::Mat centers = pixelSetCentersToMat(mSet);

	for (int idx = 0; idx < mTextLines.size(); idx++) {
		cv::Mat md = mahalanobisDists(mTextLines[idx], centers);
		//cv::normalize(md, md, 1, 0, cv::NORM_MINMAX);
		//md *= 10000;
		//cv::exp(md, md);
		//md -= 1;
		md.copyTo(data.col(idx));
	}

	//cv::normalize(data, data, 1.0, 0.0, cv::NORM_MINMAX);
	//data = IP::invert(data);

	//cv::normalize(data, data, 300000.0, 0.0, cv::NORM_MINMAX);
	//data *= config()->scaleFactor() * 100;

	////  -------------------------------------------------------------------- compute euclidean dists
	//// convert centers
	//cv::Mat centers = pixelSetCentersToMat(mSet);

	//for (int idx = 0; idx < mTextLines.size(); idx++) {
	//	cv::Mat md = euclideanDists(mTextLines[idx]);
	//	md *= config()->scaleFactor();
	//	//cv::normalize(md, md, 1, 0, cv::NORM_MINMAX);
	//	md.copyTo(data.col(idx));
	//}

	//cv::normalize(data, data, 300000.0, 0.0, cv::NORM_MINMAX);

	//cv::Mat s = data.clone();
	////s.convertTo(s, CV_32FC1);
	//cv::reduce(s, s, 0, CV_REDUCE_SUM);

	//Image::imageInfo(s, "s");
	//qDebug().noquote() << Image::printMat<double>(s, "costSums");

	// euclidean - manual --------------------------------------------------------------------
	//// cache text line centers
	//QVector<Vector2D> tCenters;
	//tCenters.reserve(mTextLines.size());
	//for (const PixelSet& s : mTextLines) {
	//	tCenters << s.center();
	//}

	//for (int idx = 0; idx < mSet.size(); idx++) {

	//	cv::Mat cData(1, numLabels, CV_32SC1);
	//	int* cPtr = cData.ptr<int>();

	//	auto px = mSet[idx];

	//	for (int tIdx = 0; tIdx < mTextLines.size(); tIdx++) {

	//		Vector2D d(tCenters[tIdx] - px->center());

	//		// local orientation weighted
	//		double a = 1.0;
	//		if (px->stats())
	//			a = std::abs(d.theta(mSet[idx]->stats()->orVec()));

	//		cPtr[tIdx] = qRound(d.length() * (a + 0.01));	// + 0.01 -> we don't want to map all 'aligned' pixels to 0
	//	}

	//	cData.convertTo(data.row(idx), CV_32SC1, config()->scaleFactor());
	//}

	//cv::normalize(data, data, 10000, 0, cv::NORM_MINMAX);

	
	data.convertTo(data, CV_32SC1);

	Image::imageInfo(data, "costs");

	return data;
}

cv::Mat GraphCutTextLine::labelDistMatrix(int numLabels) const {

	cv::Mat labelDist(numLabels, numLabels, CV_32FC1);

	//// labels using bhattacharyya distance --------------------------------------------------------------------

	//for (int rIdx = 0; rIdx < numLabels; rIdx++) {

	//	float* lp = labelDist.ptr<float>(rIdx);
	//	Pixel px(mTextLines[rIdx].fitEllipse(), mTextLines[rIdx].boundingBox(), mTextLines[rIdx].id());

	//	for (int cIdx = 0; cIdx < numLabels; cIdx++) {

	//		Pixel cpx(mTextLines[cIdx].fitEllipse(), mTextLines[cIdx].boundingBox(), mTextLines[cIdx].id());
	//		lp[cIdx] = PixelDistance::bhattacharyya(&px, &cpx);
	//	}
	//}

	double mthr = 5;
	
	for (int rIdx = 0; rIdx < numLabels; rIdx++) {

		float* lp = labelDist.ptr<float>(rIdx);
		cv::Mat icov = mTextLines[rIdx].fitEllipse().toCov();
		icov = icov.mul(icov);	// square the covariance - to prefer 'horizontally' aligned text lines
		cv::invert(icov, icov, cv::DECOMP_SVD);

		for (int cIdx = 0; cIdx < numLabels; cIdx++) {

			// compute the mahalnobis distance
			Vector2D vec(mTextLines[rIdx].center() - mTextLines[cIdx].center());
			cv::Mat nm = vec.toMatCol();
			nm = nm*icov*nm.t();

			double d = /*std::sqrt*/(nm.at<double>(0, 0));

			lp[cIdx] = d < mthr ? (float)d : mthr;

			//Vector2D vec(mTextLines[rIdx].center() - mTextLines[cIdx].center());
			//lp[cIdx] = (float)std::sqrt(vec.length());

			//if (lp[cIdx] > mthr)
			//	lp[cIdx] = mthr;
		}
	}

	//cv::normalize(labelDist, labelDist, config()->scaleFactor(), 0.0, cv::NORM_MINMAX);
	labelDist.convertTo(labelDist, CV_32SC1);

	// make it a metric (symmetric)
	int* lp = labelDist.ptr<int>();

	for (int rIdx = 0; rIdx < numLabels; rIdx++) {

		for (int cIdx = rIdx+1; cIdx < numLabels; cIdx++) {
		
			int* rl = lp + (rIdx * numLabels + cIdx);	// row label
			int* cl = lp + (rIdx + cIdx * numLabels);	// col label

			int val = qMin(*rl, *cl);
			*rl = val;
			*cl = val;
		}
	}

	Image::imageInfo(labelDist, "label costs");

	return labelDist;
}

int GraphCutTextLine::numLabels() const {

	return mTextLines.size();
}

cv::Mat GraphCutTextLine::mahalanobisDists(const PixelSet & tl, const cv::Mat& centers) const {
	
	double mthr = 150000;

	cv::Mat dists(centers.rows, 1, CV_64FC1);
	double* dp = dists.ptr<double>();

	// get the textlines mean & cov
	cv::Mat c = tl.center().toMatCol();
	cv::Mat icov = tl.fitEllipse().toCov();
	//icov = icov.mul(icov);
	cv::invert(icov, icov, cv::DECOMP_SVD);

	for (int idx = 0; idx < centers.rows; idx++) {

		// compute the mahalnobis distance
		cv::Mat nm = centers.row(idx) - c;
		nm = nm*icov*nm.t();

		double d = /*std::exp*/(nm.at<double>(0, 0));
		dp[idx] = d < mthr ? d : mthr;
	}

	return dists;
}

cv::Mat GraphCutTextLine::euclideanDists(const PixelSet & tl) const {
	
	cv::Mat dists(mSet.size(), 1, CV_64FC1);
	double* dp = dists.ptr<double>();

	Vector2D c = tl.center();

	for (int idx = 0; idx < mSet.size(); idx++) {

		auto px = mSet[idx];
		Vector2D d(c - px->center());

		// local orientation weighted
		double a = 1.0;
		if (px->stats())
			a = std::abs(d.theta(mSet[idx]->stats()->orVec()));

		dp[idx] = qRound(d.length() * (a + 0.01));	// + 0.01 -> we don't want to map all 'aligned' pixels to 0
	}

	return dists;
}

/// <summary>
/// Converts the pixel centers into a mat.
/// </summary>
/// <param name="set">The pixel set.</param>
/// <returns>a N x 2 CV_64FC1 center mat.</returns>
cv::Mat GraphCutTextLine::pixelSetCentersToMat(const PixelSet & set) const {
	
	cv::Mat centers(set.size(), 2, CV_64FC1);
	double* cp = centers.ptr<double>();

	auto pixels = set.pixels();

	for (int idx = 0; idx < pixels.size(); idx++) {
		
		*cp = pixels[idx]->center().x(); cp++;
		*cp = pixels[idx]->center().y(); cp++;
	}

	return centers;
}

void GraphCutTextLine::saveDistsDebug(const QString & filePath, const cv::Mat& img) const {

	// sort w.r.t y
	QVector<PixelSet> tls = mTextLines;
	//std::sort(tls.begin(), tls.end(),
	//	[](const PixelSet& ps1, const PixelSet& ps2) {
	//	return ps1.center().y() < ps2.center().y();
	//}
	//);


	cv::Mat labelDist(numLabels(), numLabels(), CV_32FC1);

	//for (int rIdx = 0; rIdx < numLabels(); rIdx++) {

	//	float* lp = labelDist.ptr<float>(rIdx);
	//	Ellipse e = tls[rIdx].fitEllipse();
	//	//Vector2D a = e.axis();
	//	//a.setX(a.x()*a.x());
	//	//e.setAxis(a);

	//	cv::Mat icov = e.toCov();
	//	icov = icov.mul(icov);
	//	cv::invert(icov, icov, cv::DECOMP_SVD);

	//	for (int cIdx = 0; cIdx < numLabels(); cIdx++) {

	//		//cv::Mat ccov = mTextLines[cIdx].fitEllipse().toCov();
	//		//ccov += rcov;
	//		//ccov /= 2.0;


	//		// compute the mahalnobis distance
	//		//cv::Mat nm = centers.row(idx) - c;
	//		Vector2D vec(mTextLines[rIdx].center() - mTextLines[cIdx].center());
	//		cv::Mat nm = vec.toMatCol();
	//		nm = nm*icov*nm.t();

	//		double d = nm.at<double>(0, 0);

	//		lp[cIdx] = (float)std::sqrt(d);
	//		//Vector2D vec(tls[rIdx].center() - tls[cIdx].center());
	//		//lp[cIdx] = (float)std::sqrt(vec.length())*2;
	//	}
	//}

	for (int rIdx = 0; rIdx < numLabels(); rIdx++) {

		float* lp = labelDist.ptr<float>(rIdx);
		Pixel px(mTextLines[rIdx].fitEllipse(), mTextLines[rIdx].boundingBox(), mTextLines[rIdx].id());

		for (int cIdx = 0; cIdx < numLabels(); cIdx++) {

			Pixel cpx(mTextLines[cIdx].fitEllipse(), mTextLines[cIdx].boundingBox(), mTextLines[cIdx].id());
			lp[cIdx] = PixelDistance::bhattacharyya(&px, &cpx);
		}
	}

	int idx = 0;
	for (auto tl : tls) {

		QPixmap pm = Image::mat2QPixmap(img);
		QPainter p(&pm);

		// the next lines show the mahalanobis distance
		//cv::Mat d = mahalanobisDists(tl, pixelSetCentersToMat(mSet));
		//cv::Mat d = euclideanDists(tl);
		cv::Mat d = labelDist.row(idx);
		cv::Mat ds = d.clone();
		cv::sort(ds, ds, CV_SORT_ASCENDING);
		double v5 = ds.at<float>(0, 2);

		float* dp = d.ptr<float>();

		for (int i = 0; i < d.cols; i++) {
			int v = qMin(qRound(dp[i]*5), 255);

			if (dp[i] > v5)
				p.setPen(QColor(v, 0, 0));
			else
				p.setPen(ColorManager::pink());

			//mSet.pixels()[i]->draw(p);
			tls[i].draw(p);
		}

		// draw currently selected text line
		p.setOpacity(1.0);
		p.setPen(ColorManager::blue());
		//tl.fitEllipse().draw(p, 0.1);
		tls[idx].draw(p, PixelSet::draw_poly);

		QString sp = rdf::Utils::instance().createFilePath(filePath, QString::number(idx));
		Image::save(Image::qPixmap2Mat(pm), sp);
		idx++;
		qDebug() << "writing" << sp;
	}

}

}