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

#include "DebugMarkus.h"
#include "PageParser.h"
#include "Utils.h"
#include "Image.h"
#include "Binarization.h"
#include "LineTrace.h"
#include "Elements.h"
#include "ElementsHelper.h"
#include "Settings.h"
#include "GraphCut.h"

#include "SuperPixelScaleSpace.h"
#include "TabStopAnalysis.h"
#include "TextLineSegmentation.h"
#include "PageSegmentation.h"
#include "SuperPixelClassification.h"
#include "SuperPixelTrainer.h"
#include "LayoutAnalysis.h"
#include "lsd/LSDDetector.h"

#pragma warning(push, 0)	// no warnings from includes
#include <QDebug>
#include <QImage>
#include <QFileInfo>
#include <QProcess>

#include <QJsonObject>

#include <opencv2/imgproc.hpp>
#include <opencv2/ml.hpp>
#pragma warning(pop)

namespace rdf {

XmlTest::XmlTest(const DebugConfig& config) {

	mConfig = config;
}

void XmlTest::parseXml() {

	rdf::Timer dt;

	// test image loading
	QImage img(mConfig.imagePath());
	cv::Mat imgCv = Image::qImage2Mat(img);

	if (!imgCv.empty())
		qInfo() << mConfig.imagePath() << "loaded...";
	else
		qInfo() << mConfig.imagePath() << "NOT loaded...";

	// parse xml
	PageXmlParser parser;
	parser.read(mConfig.xmlPath());

	auto root = parser.page()->rootRegion();
	auto regions = rdf::RegionManager::filter<rdf::Region>(parser.page()->rootRegion(), Region::type_text_region);

	for (auto r : regions)
		r->removeAllChildren();
	qDebug() << regions.size() << "/" << root->children().size() << "remain";
	
	root->setChildren(regions);
	parser.write(mConfig.xmlPath(), parser.page());

	qDebug() << mConfig.xmlPath() << "parsed in " << dt;
}

void XmlTest::linesToXml() {

	rdf::Timer dt;

	qDebug() << "image path: " << mConfig.imagePath();

	// load image
	QImage img(mConfig.imagePath());
	cv::Mat imgCv = Image::qImage2Mat(img);

	// binarize
	rdf::BinarizationSuAdapted binarizeImg(imgCv, cv::Mat());
	binarizeImg.compute();
	cv::Mat bwImg = binarizeImg.binaryImage();
	qInfo() << "binarised in" << dt;

	// find lines
	rdf::LineTrace lt(bwImg);
	lt.setAngle(0.0);
	lt.compute();
	QVector<rdf::Line> allLines = lt.getLines();
	qInfo() << allLines.size() << "lines detected in" << dt;

	// init parser
	PageXmlParser parser;
	parser.read(mConfig.xmlPath());

	auto root = parser.page()->rootRegion();

	// test writing lines
	for (const rdf::Line& cL : allLines) {

		QSharedPointer<rdf::SeparatorRegion> pSepR(new rdf::SeparatorRegion());
		pSepR->setLine(cL.qLine());
		root->addUniqueChild(pSepR);
	}

	parser.write(PageXmlParser::imagePathToXmlPath(mConfig.outputPath()), parser.page());
	
	qInfo() << mConfig.imagePath() << "lines computed and written in" << dt;
}

// Layout Test --------------------------------------------------------------------
LayoutTest::LayoutTest(const DebugConfig & config) {
	mConfig = config;
}

void LayoutTest::testComponents() {

	Timer dt;

	// test image loading
	QImage img(mConfig.imagePath());
	cv::Mat imgCv = Image::qImage2Mat(img);

	if (!imgCv.empty())
		qInfo() << mConfig.imagePath() << "loaded...";
	else
		qInfo() << mConfig.imagePath() << "NOT loaded...";

	// switch tests
	//testFeatureCollector(imgCv);
	//testTrainer();
	//pageSegmentation(imgCv);
	//testLayout(imgCv);
	layoutToXml();
	//layoutToXmlDebug();
	//testLineDetector(imgCv);

	//eval();

	qInfo() << "total computation time:" << dt;
}

void LayoutTest::layoutToXml() const {

	QImage imgQt(mConfig.imagePath());
	cv::Mat img = Image::qImage2Mat(imgQt);

	if (img.empty()) {
		qWarning() << "could not load image from" << mConfig.imagePath();
		return;
	}

	Timer dt;
	QString loadXmlPath = rdf::PageXmlParser::imagePathToXmlPath(mConfig.imagePath());

	if (QFileInfo(mConfig.xmlPath()).exists())
		loadXmlPath = mConfig.xmlPath();

	rdf::PageXmlParser parser;
	parser.read(loadXmlPath);
	auto pe = parser.page();

	rdf::LayoutAnalysis la(img);
	la.setRootRegion(pe->rootRegion());
	la.config()->saveDefaultSettings();	// save default layout settings

	if (!la.compute())
		qWarning() << "could not compute layout analysis";

	// drawing --------------------------------------------------------------------
	cv::Mat rImg = img.clone();

	// save super pixel image
	//rImg = superPixel.drawSuperPixels(rImg);
	//rImg = tabStops.draw(rImg);
	rImg = la.draw(rImg);
	QString dstPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-textlines");
	rdf::Image::save(rImg, dstPath);

	dstPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-image");
	rdf::Image::save(img, dstPath);

	qDebug() << "debug image saved: " << dstPath;

	// write to XML --------------------------------------------------------------------
	pe->setCreator(QString("CVL"));
	pe->setImageSize(QSize(img.cols, img.rows));
	pe->setImageFileName(QFileInfo(mConfig.imagePath()).fileName());

	//pe->setRootRegion(la.textBlockSet().toTextRegion());

	auto root = la.textBlockSet().toTextRegion();
	
	for (const QSharedPointer<rdf::Region>& r : root->children()) {
		
		if (!pe->rootRegion()->reassignChild(r))
			pe->rootRegion()->addUniqueChild(r, true);
	}

	parser.write(mConfig.xmlPath(), pe);

	qInfo() << "layout analysis computed in" << dt;
}

void LayoutTest::layoutToXmlDebug() const {

	QImage imgQt(mConfig.imagePath());
	cv::Mat img = Image::qImage2Mat(imgQt);

	Timer dt;
	QString loadXmlPath = rdf::PageXmlParser::imagePathToXmlPath(mConfig.imagePath());

	if (QFileInfo(mConfig.xmlPath()).exists())
		loadXmlPath = mConfig.xmlPath();


	rdf::PageXmlParser parser;
	parser.read(loadXmlPath);
	auto pe = parser.page();

	// start computing --------------------------------------------------------------------
	rdf::GridSuperPixel gpm(img);

	if (!gpm.compute())
		qWarning() << "could not compute" << mConfig.imagePath();

	// read back the model
	QSharedPointer<SuperPixelModel> model = SuperPixelModel::read(mConfig.classifierPath());

	auto f = model->model();
	if (f->isTrained())
		qDebug() << "the classifier I loaded is trained...";

	SuperPixelClassifier spc(img, gpm.pixelSet());
	spc.setModel(model);

	if (!spc.compute())
		qWarning() << "could not classify SuperPixels";


	// end computing --------------------------------------------------------------------

	//// debug visualizations --------------------------------------------------------------------

	//Timer dte;
	//cv::Mat ei(img.size(), CV_32FC1, cv::Scalar(0));
	//QImage qImg = Image::mat2QImage(img, true);
	//QPainter p(&qImg);
	//p.setPen(ColorManager::white());
	//p.setOpacity(0.1);

	//for (auto px : pixels.pixels()) {

	//	double ls = px->stats()->lineSpacing();
	//	double lr = px->ellipse().radius();
	//	Vector2D sVec(std::sqrt(ls), lr*3);
	//	Ellipse e(px->center(), sVec, -px->stats()->orientation());
	//	//Ellipse e(px->ellipse());
	//	e.draw(p, 1.0);
	//	e.pdf(ei);
	//}

	//qDebug() << "line map created in" << dte;

	//cv::normalize(ei, ei, 1.0, 0.0, cv::NORM_MINMAX);
	//cv::Mat dImg = ei;
	//cv::Mat gcImg = Image::qImage2Mat(qImg);


	//// debug visualizations --------------------------------------------------------------------
	
	// drawing --------------------------------------------------------------------

	cv::Mat gcImg;
	//gcImg = gpm.draw(img, ColorManager::blue());
	gcImg = spc.draw(img);

	QString dstPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-classified");
	rdf::Image::save(gcImg, dstPath);
	qDebug() << "pixel image saved: " << dstPath;

	if (dstPath.contains(WHO_IS_CUTE, Qt::CaseInsensitive))
		qDebug() << "b.t.w. best image name ever...";
	
	qInfo() << "layout analysis computed in" << dt;
}

void LayoutTest::testFeatureCollector(const cv::Mat & src) const {
	
	rdf::Timer dt;

	// parse xml
	PageXmlParser parser;
	parser.read(mConfig.xmlPath());

	// test loading of label lookup
	LabelManager lm = LabelManager::read(mConfig.labelConfigPath());
	qInfo().noquote() << lm.toString();

	// compute super pixels
	GridSuperPixel sp(src);

	if (!sp.compute())
		qCritical() << "could not compute super pixels!";

	// feed the label lookup
	SuperPixelLabeler spl(sp.pixelSet(), Rect(src));
	spl.setLabelManager(lm);
	spl.setFilePath(mConfig.imagePath());

	// set the ground truth
	if (parser.page())
		spl.setRootRegion(parser.page()->rootRegion());

	if (!spl.compute())
		qCritical() << "could not compute SuperPixel labeling!";

	SuperPixelFeature spf(src, spl.set());
	if (!spf.compute())
		qCritical() << "could not compute SuperPixel features!";

	FeatureCollectionManager fcm(spf.features(), spf.pixelSet());
	fcm.write(mConfig.featureCachePath());
	
	FeatureCollectionManager testFcm = FeatureCollectionManager::read(mConfig.featureCachePath());

	for (int idx = 0; idx < testFcm.collection().size(); idx++) {

		if (testFcm.collection()[idx].label() != fcm.collection()[idx].label())
			qWarning() << "wrong labels!" << testFcm.collection()[idx].label() << "vs" << fcm.collection()[idx].label();
		else
			qInfo() << testFcm.collection()[idx].label() << "is fine...";
	}

	// drawing
	cv::Mat rImg = src.clone();

	// save super pixel image
	//rImg = superPixel.drawSuperPixels(rImg);
	//rImg = tabStops.draw(rImg);
	rImg = spl.draw(rImg);
	rImg = spf.draw(rImg);
	QString dstPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-textlines");
	rdf::Image::save(rImg, dstPath);
	qDebug() << "debug image saved: " << dstPath;

	qDebug() << "image path: " << mConfig.imagePath();
}

void LayoutTest::testTrainer() {

	//cv::Mat testM(10, 10, CV_8UC1);
	//
	//for (int rIdx = 0; rIdx < testM.rows; rIdx++) {
	//	unsigned char* ptr = testM.ptr<unsigned char>(rIdx);
	//	for (int cIdx = 0; cIdx < testM.cols; cIdx++) {
	//		ptr[cIdx] = cIdx*rIdx+cIdx;
	//	}
	//}
	//
	//QJsonObject jo = Image::matToJson(testM);
	//cv::Mat t2 = Image::jsonToMat(jo);

	//cv::Scalar s = cv::sum(cv::abs(testM - t2));
	//if (s[0] != 0)
	//	qWarning() << "inconsistent json2Mat I/O";
	//else
	//	qInfo() << "json to mat is just fine...";

	
	Timer dt;
	FeatureCollectionManager fcm = FeatureCollectionManager::read(mConfig.featureCachePath());
	

	// train classifier
	SuperPixelTrainer spt(fcm);

	if (!spt.compute())
		qCritical() << "could not train data...";

	spt.write(mConfig.classifierPath());
	
	// read back the model
	QSharedPointer<SuperPixelModel> model = SuperPixelModel::read(mConfig.classifierPath());

	auto f = model->model();
	if (f->isTrained())
		qDebug() << "the classifier I loaded is trained...";
	
	//qDebug() << fcm.numFeatures() << "SuperPixels trained in" << dt;
}

void LayoutTest::testLineDetector(const cv::Mat & src) const {

	Timer dt;
	cv::Mat img = src.clone();

	rdf::LineTraceLSD lsd(img);
	
	if (!lsd.compute())
		qDebug() << "could not compute LSD line detector";

	cv::Mat rImg = img.clone();
	rImg = lsd.draw(rImg);

	QString dstPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-lines");
	rdf::Image::save(rImg, dstPath);
	qDebug() << "debug image saved: " << dstPath;

	// compute lines - old school

	if (img.depth() != CV_8U) {
		img.convertTo(img, CV_8U, 255);
	}

	if (img.channels() != 1) {
		cv::cvtColor(img, img, CV_RGB2GRAY);
	}

	double skewAngle = 0.0;

	rdf::BinarizationSuAdapted bm(img, cv::Mat());
	bm.compute();
	cv::Mat bwImg = bm.binaryImage();

	rdf::LineTrace lt(bwImg, cv::Mat());

	lt.setAngle(skewAngle);
	lt.compute();

	QVector<rdf::Line> alllines = lt.getLines();

	// visualize
	cv::Mat synLine = lt.generatedLineImage();

	//visualize
	if (synLine.channels() == 1)
		cv::cvtColor(synLine, synLine, CV_GRAY2BGRA);

	dstPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-bw-lines", "jpg");
	rdf::Image::save(synLine, dstPath);

	qDebug() << "line detector computed in" << dt;

}

void LayoutTest::testLayout(const cv::Mat & src) const {

	// TODOS
	// - DBScan is very sensitive to the line spacing
	
	// Workflow:
	// - implement noise/text etc classification on SuperPixel level
	// - smooth labels using graphcut
	// - perform everything else without noise pixels
	// Training:
	// - open mode (whole image only contains e.g. machine printed)
	// - baseline mode -> overlap with superpixel

	cv::Mat img = src.clone();
	//cv::resize(src, img, cv::Size(), 0.25, 0.25, CV_INTER_AREA);

	Timer dt;

	// find super pixels
	//rdf::SuperPixel superPixel(img);
	rdf::ScaleSpaceSuperPixel<SuperPixel> superPixel(img);
	
	if (!superPixel.compute())
		qWarning() << "could not compute super pixel!";

	PixelSet sp = superPixel.pixelSet();

	// find local orientation per pixel
	rdf::LocalOrientation lo(sp.pixels());
	if (!lo.compute())
		qWarning() << "could not compute local orientation";

	// smooth estimation
	rdf::GraphCutOrientation pse(sp.pixels());
	
	if (!pse.compute())
		qWarning() << "could not compute set orientation";
	
	// drawing
	cv::Mat nImg = img.clone();

	//// draw edges
	//rImg = textBlocks.draw(rImg);
	//// save super pixel image
	nImg = superPixel.draw(nImg);

	//rImg = tabStops.draw(rImg);
	//rImg = spc.draw(rImg);
	QString imgPathN = rdf::Utils::createFilePath(mConfig.outputPath(), "-nomacs", "png");
	rdf::Image::save(nImg, imgPathN);
	qDebug() << "debug image added" << imgPathN;

	// pixel labeling
	QSharedPointer<SuperPixelModel> model = SuperPixelModel::read(mConfig.classifierPath());

	SuperPixelClassifier spc(src, sp);
	spc.setModel(model);

	if (!spc.compute())
		qWarning() << "could not classify SuperPixels";

	//// find tab stops
	//rdf::TabStopAnalysis tabStops(sp);
	//if (!tabStops.compute())
	//	qWarning() << "could not compute text block segmentation!";


	// find text lines
	rdf::TextLineSegmentation textLines(sp.pixels());
	//textLines.addSeparatorLines(tabStops.tabStopLines(30));	// TODO: fix parameter

	if (!textLines.compute(img))
		qWarning() << "could not compute text line segmentation!";

	qInfo() << "algorithm computation time" << dt;

	// drawing
	cv::Mat rImg = img.clone();
	rImg = textLines.draw(rImg);

	//// draw edges
	//rImg = textBlocks.draw(rImg);
	//// save super pixel image
	//rImg = superPixel.draw(rImg);
	//rImg = tabStops.draw(rImg);
	rImg = spc.draw(rImg);
	rImg = textLines.draw(rImg);
	QString imgPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-tlc");
	rdf::Image::save(rImg, imgPath);
	qDebug() << "debug image added" << imgPath;

	// draw a second image ---------------------------------
	rImg = img.clone();

	rImg = spc.draw(rImg);
	imgPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-sp");
	rdf::Image::save(rImg, imgPath);
	qDebug() << "debug image added" << imgPath;

	//// write XML -----------------------------------
	//QString loadXmlPath = rdf::PageXmlParser::imagePathToXmlPath(mConfig.imagePath());

	//rdf::PageXmlParser parser;
	//parser.read(loadXmlPath);
	//auto pe = parser.page();
	//pe->setCreator(QString("CVL"));
	//pe->setImageSize(QSize(img.rows, img.cols));
	//pe->setImageFileName(QFileInfo(mConfig.imagePath()).fileName());

	//// start writing content
	//auto ps = PixelSet::fromEdges(PixelSet::connect(sp, Rect(0, 0, img.cols, img.rows)));

	//if (!ps.empty()) {
	//	QSharedPointer<Region> textRegion = QSharedPointer<Region>(new Region());
	//	textRegion->setType(Region::type_text_region);
	//	textRegion->setPolygon(ps[0]->convexHull());
	//	
	//	for (auto tl : textLines.textLines()) {
	//		textRegion->addUniqueChild(tl);
	//	}

	//	pe->rootRegion()->addUniqueChild(textRegion);
	//}

	//parser.write(mConfig.xmlPath(), pe);
	//qDebug() << "results written to" << mConfig.xmlPath();

}

void LayoutTest::pageSegmentation(const cv::Mat & src) const {

	// TODOS
	// - line spacing needs smoothing -> graphcut
	// - DBScan is very sensitive to the line spacing

	// Workflow:
	// - implement noise/text etc classification on SuperPixel level
	// - smooth labels using graphcut
	// - perform everything else without noise pixels
	// Training:
	// - open mode (whole image only contains e.g. machine printed)
	// - baseline mode -> overlap with superpixel

	cv::Mat img = src.clone();
	//cv::resize(src, img, cv::Size(), 0.25, 0.25, CV_INTER_AREA);

	Timer dt;

	// find super pixels
	rdf::PageSegmentation pageSeg(img);

	if (!pageSeg.compute())
		qWarning() << "could not compute page segmentation!";

	qInfo() << "algorithm computation time" << dt;

	// drawing
	cv::Mat rImg = img.clone();

	// save super pixel image
	rImg = pageSeg.draw(rImg);
	QString maskPath = rdf::Utils::createFilePath(mConfig.outputPath(), "-page-seg");
	rdf::Image::save(rImg, maskPath);
	qDebug() << "debug image added" << maskPath;
}

double LayoutTest::scaleFactor(const cv::Mat& img) const {

	int cms = 3000;

	if (cms > 0) {

		if (cms < 500) {
			qWarning() << "you chose the maximal image side to be" << cms << "px - this is pretty low";
		}

		// find the image side
		int mSide = 0;
		mSide = img.rows;

		double sf = (double)cms / mSide;

		// do not rescale if the factor is close to 1
		if (sf <= 0.95)
			return sf;

		// inform user that we do not resize if the scale factor is close to 1
		if (sf < 1.0)
			qInfo() << "I won't resize the image since the scale factor is" << sf;
	}

	return 1.0;
}

void LayoutTest::eval() const {
	
	QString metricPath = "D:/read/evalTools/BaseLineEval/lineMetric.jar";
	QString gtPath = rdf::PageXmlParser::imagePathToXmlPath(mConfig.imagePath());
	QString xmlPath = mConfig.xmlPath();

	if (QFileInfo(metricPath).exists() &&
		QFileInfo(gtPath).exists() &&
		QFileInfo(xmlPath).exists()) {
		eval(metricPath, gtPath, xmlPath);
	}
	else if (!QFileInfo(gtPath).exists()) {
		qDebug() << gtPath << "does not exist";
	}
	else if (!QFileInfo(gtPath).exists()) {
		qDebug() << gtPath << "does not exist";
	}
	else if (!QFileInfo(xmlPath).exists()) {
		qDebug() << xmlPath << "does not exist";
	}

}

void LayoutTest::eval(const QString & toolPath, const QString & gtPath, const QString & resultPath) const {

	QStringList params;
	params << "/c";
	params << "java";
	params << "-jar";
	params << toolPath;
	params << gtPath;
	params << resultPath;

	QProcess eval;
	eval.start("cmd", params);
	eval.waitForFinished();

	//qDebug() << "cmd: " << params;
	//qDebug() << "exit status: " << eval.exitStatus() << "code:" << eval.exitCode();

	// show the results
	qDebug().noquote() << "\n" << eval.readAllStandardOutput();
}

}