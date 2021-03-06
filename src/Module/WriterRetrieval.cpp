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

#include "WriterRetrieval.h"
#include "PageParser.h"
#include "Elements.h"

//opencv
#include <opencv2/imgproc.hpp>
#ifdef WITH_XFEATURES2D
#include <opencv2/xfeatures2d.hpp>
#endif

#pragma warning(push, 0)	// no warnings from includes
// Qt Includes
#include <QSharedPointer>
#include <QVector>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QPolygon>
#pragma warning(pop)

namespace rdf {
	/// <summary>
	/// Initializes a new instance of the <see cref="WriterIdentification"/> class.
	/// </summary>
	WriterImage::WriterImage() {
		// do nothing
	}
	/// <summary>
	/// Sets the image.
	/// </summary>
	/// <param name="img">The img.</param>
	void WriterImage::setImage(const cv::Mat img) {
		this->mImg = img;
	}
	/// <summary>
	/// Calculates the SIFT features of the image.
	/// </summary>
	void WriterImage::calculateFeatures() {
		if(mImg.empty())
			return;
#ifdef WITH_XFEATURES2D
		cv::Ptr<cv::Feature2D> sift = cv::xfeatures2d::SIFT::create();
		std::vector<cv::KeyPoint> kp;
		sift->detect(mImg, kp, cv::Mat());


		qWarning() << "using modified SIFT";

		for(int i = 0; i < kp.size(); i++) {
			if(kp[i].angle > 180)
				kp[i].angle -= 180;
			//kp[i].angle = 0;
			//kp[i].size = 10;
		}

		sift->compute(mImg, kp, mDescriptors);

		mKeyPoints = QVector<cv::KeyPoint>::fromStdVector(kp);
#else
		mKeyPoints = QVector<cv::KeyPoint>();
		qWarning() << "ReadFramework is compilied without xfeatures2d. Thus, WriterRetrieval is not able to generate features";
		#pragma message("[WARNING] Writer Retrieval is not working when compiling without xfeatures2d! Please enable it in cmake")
#endif

		
	}
	/// <summary>
	/// Saves the SIFT features to the given file path.
	/// </summary>
	/// <param name="filePath">The file path.</param>
	void WriterImage::saveFeatures(QString filePath) {
		if(mKeyPoints.empty() || mDescriptors.empty()) {
			qWarning() << debugName() << " keypoints or descriptors empty ... unable to save to file";
			return;
		}
		QFileInfo fi(filePath);
		if(!fi.absoluteDir().exists()) {
			qWarning() << debugName() << " unable to save features, directory " << fi.absoluteDir().path() << "does not exist";
		}
		cv::FileStorage fs(filePath.toStdString(), cv::FileStorage::WRITE);
		fs << "keypoints" << mKeyPoints.toStdVector();
		fs << "descriptors" << mDescriptors;
		fs.release();
	}
	/// <summary>
	/// Loads the features from the given file path.
	/// </summary>
	/// <param name="filePath">The file path.</param>
	void WriterImage::loadFeatures(QString filePath) {
		cv::FileStorage fs(filePath.toStdString(), cv::FileStorage::READ);
		if(!fs.isOpened()) {
			qWarning() << debugName() << " unable to read file " << filePath;
			return;
		}
		std::vector<cv::KeyPoint> kp;
		fs["keypoints"] >> kp;
		mKeyPoints = QVector<cv::KeyPoint>::fromStdVector(kp);
		fs["descriptors"] >> mDescriptors;
		fs.release();
	}
	/// <summary>
	/// Sets the key points for this Writer Identification task.
	/// </summary>
	/// <param name="kp">The keypoints.</param>
	void WriterImage::setKeyPoints(QVector<cv::KeyPoint> kp) {
		mKeyPoints = kp;
	}
	/// <summary>
	/// Returns the keypoints of the SIFT features.
	/// </summary>
	/// <returns>keypoints</returns>
	QVector<cv::KeyPoint> WriterImage::keyPoints() const {
		return mKeyPoints;
	}
	/// <summary>
	/// Sets the descriptors for this Writer Identification task.
	/// </summary>
	/// <param name="desc">The descriptors.</param>
	void WriterImage::setDescriptors(cv::Mat desc) {
		mDescriptors = desc;
	}
	/// <summary>
	/// Returns the descriptors of the SIFT features
	/// </summary>
	/// <returns></returns>
	cv::Mat WriterImage::descriptors() const {
		return mDescriptors;
	}
	/// <summary>
	/// Filters the key points according to the size of the keypoints
	/// </summary>
	/// <param name="minSize">The minimum size of the keypoints</param>
	/// <param name="maxSize">The maximum size of the keypoints (-1 to ignore)</param>
	void WriterImage::filterKeyPoints(int minSize, int maxSize) {
		cv::Mat filteredDesc = cv::Mat(0, mDescriptors.cols, mDescriptors.type());
		int r = 0;
		for(auto kpItr = mKeyPoints.begin(); kpItr != mKeyPoints.end(); r++) {
			if(kpItr->size*1.5 * 4 > maxSize && maxSize > 0) {
				kpItr = mKeyPoints.erase(kpItr);
			}
			else if(kpItr->size * 1.5 * 4 < minSize) {
				kpItr = mKeyPoints.erase(kpItr);
			}
			else {
				kpItr++;
				filteredDesc.push_back(mDescriptors.row(r).clone());
			}
		}
		mInfo << "filtered " << mDescriptors.rows - filteredDesc.rows << " SIFT features (maxSize:" << maxSize << " minSize:" << minSize << ")";
		mDescriptors = filteredDesc;
	}
	/// <summary>
	/// Filters out key points which are not inside one of the polygons in the vector
	/// </summary>
	/// <param name="polys">Vector of polygons (text regions).</param>
	void WriterImage::filterKeyPointsPoly(QVector<QPolygonF> polys) {
		if(polys.isEmpty())
			return;
		cv::Mat filteredDesc = cv::Mat(0, mDescriptors.cols, mDescriptors.type());
		QVector<cv::KeyPoint> filteredKeyPoints;
		for(int i = 0; i < polys.size(); i++) {
			for(int j = 0; j < mKeyPoints.size(); j++) {
				const QPointF p = QPointF(mKeyPoints[j].pt.x, mKeyPoints[j].pt.y);
				if(polys[i].containsPoint(p, Qt::FillRule::WindingFill)) {
					filteredKeyPoints.push_back(mKeyPoints[j]);
					filteredDesc.push_back(mDescriptors.row(j).clone());
				}
			}
		}
		mKeyPoints = filteredKeyPoints;
		mDescriptors = filteredDesc;
	}
	/// <summary>
	/// Debug name
	/// </summary>
	/// <returns></returns>
	QString WriterImage::debugName() {
		return "WriterImage";
	}

	// --------------- WriterRetrievalConfig -----------------------------------------------------------------------------
	/// <summary>
	/// Initializes a new instance of the <see cref="WriterRetrievalConfig"/> class.
	/// </summary>
	WriterRetrievalConfig::WriterRetrievalConfig() : ModuleConfig("WriterRetrieval") {
	}
	/// <summary>
	/// outputs the feature directory and the properties of the vocabulary
	/// </summary>
	/// <returns></returns>
	QString WriterRetrievalConfig::toString() const {
		return "featureDir:" + mFeatureDir + " " + mVoc.toString();
	}
	/// <summary>
	/// Determines whether this instance is empty.
	/// </summary>
	/// <returns></returns>
	bool WriterRetrievalConfig::isEmpty() {
		return mVoc.isEmpty();
	}
	/// <summary>
	/// returns the vocabulary
	/// </summary>
	/// <returns></returns>
	WriterVocabulary WriterRetrievalConfig::vocabulary() {
		return mVoc;
	}
	/// <summary>
	/// Loads the properties from the settings. If a vocabulary path is set, the vocabulary is also loaded.
	/// </summary>
	/// <param name="settings">The settings.</param>
	void WriterRetrievalConfig::load(const QSettings & settings) {
		mSettingsVocPath = settings.value("vocPath", QString()).toString();
		if(!mSettingsVocPath.isEmpty()) {
			mInfo << "loading vocabulary from " << mSettingsVocPath;
			if(!QFileInfo(mSettingsVocPath).exists()) 
				mWarning << "vocabulary " << mSettingsVocPath << " does not exist!";
			else
				mVoc.loadVocabulary(mSettingsVocPath);
		} else {
			mVoc.setType(settings.value("vocType", mVoc.type()).toInt());
			if(mVoc.type() > rdf::WriterVocabulary::WI_UNDEFINED)
				mVoc.setType(rdf::WriterVocabulary::WI_UNDEFINED);
			mVoc.setNumberOfCluster(settings.value("numberOfClusters", mVoc.numberOfCluster()).toInt());
			mVoc.setNumberOfPCA(settings.value("numberOfPCA", mVoc.numberOfPCA()).toInt());
			mVoc.setMaximumSIFTSize(settings.value("maxSIFTSize", mVoc.maximumSIFTSize()).toInt());
			mVoc.setMinimumSIFTSize(settings.value("minSIFTSize", mVoc.minimumSIFTSize()).toInt());
			mVoc.setPowerNormalization(settings.value("powerNormalization", mVoc.powerNormalization()).toInt());
			mVoc.setNumOfPCAWhiteComp(settings.value("numberOfPCAWhitening", mVoc.numberOfPCAWhiteningComponents()).toInt());
			mInfo << "settings set to: type: " << mVoc.type() << " numberOfClusters: " << mVoc.numberOfCluster() << " numberOfPCA:" << mVoc.numberOfPCA();
		}

		
		mFeatureDir = settings.value("featureDir", QString()).toString();

		mEvalFile = settings.value("evalFile", QString()).toString();
	}
	/// <summary>
	/// Saves the current setup into the settings
	/// </summary>
	/// <param name="settings">The settings.</param>
	void WriterRetrievalConfig::save(QSettings & settings) const {
		settings.setValue("vocType", mVoc.type());
		settings.setValue("numberOfClusters", mVoc.numberOfCluster());
		settings.setValue("numberOfPCA", mVoc.numberOfPCA());
		settings.setValue("maxSIFTSize", mVoc.maximumSIFTSize());
		settings.setValue("minSIFTSize", mVoc.minimumSIFTSize());
		settings.setValue("powerNormalization", mVoc.powerNormalization());

		settings.setValue("vocPath", mSettingsVocPath);
		settings.setValue("featureDir", mFeatureDir);
		settings.setValue("evalFile", mEvalFile);
	}
	/// <summary>
	/// Returns the debugName
	/// </summary>
	/// <returns></returns>
	QString WriterRetrievalConfig::debugName() {
		return mDebugName;
	}

	// --------------- WriterRetrieval ----------------------------------------------------------------------------------

	/// <summary>
	/// Initializes a new instance of the <see cref="WriterRetrieval"/> class.
	/// </summary>
	/// <param name="img">The img.</param>
	WriterRetrieval::WriterRetrieval(cv::Mat img) : Module() {
		mImg = img;
		mConfig = QSharedPointer<WriterRetrievalConfig>::create();
	}
	/// <summary>
	/// Determines whether this instance is empty (if a config is set).
	/// </summary>
	/// <returns></returns>
	bool WriterRetrieval::isEmpty() const {
		return config()->isEmpty();
	}
	/// <summary>
	/// Computes feature for the image. If a xmlPath is set the keypoints are also filterd according to the text regions.
	/// </summary>
	/// <returns></returns>
	bool WriterRetrieval::compute() {
		mInfo << "computing writer retrieval";
		if (isEmpty())
			return false;

		WriterImage wi = WriterImage();
		wi.setImage(mImg);
		wi.calculateFeatures();
		wi.filterKeyPoints(config()->vocabulary().minimumSIFTSize(), config()->vocabulary().maximumSIFTSize());
		
		// collect all polygons in xml file and filter keypoints
		QVector<QPolygonF> polys;
		if(!mXmlPath.isEmpty()) {
			PageXmlParser parser;
			parser.read(mXmlPath); // TODO: config()->xmlPath

			QSharedPointer<Region> root = parser.page()->rootRegion();
			for (QSharedPointer<Region> c : root->children()) {
				if(c->type() == Region::type_text_region) {
					polys.push_back(c->polygon().polygon());
				}
			}
			if (!polys.isEmpty())
				wi.filterKeyPointsPoly(polys);
		}
		
		mFeature = config()->vocabulary().generateHist(wi.descriptors());

		mDesc = wi.descriptors();
		mKeyPoints = wi.keyPoints();
		return true;
	}
	/// <summary>
	/// Returns the WriterRetrievalConfig
	/// </summary>
	/// <returns></returns>
	QSharedPointer<WriterRetrievalConfig> WriterRetrieval::config() const {
		return qSharedPointerDynamicCast<WriterRetrievalConfig>(mConfig);
	}
	/// <summary>
	/// Returns the feature (does not calculate the feature!)
	/// </summary>
	/// <returns></returns>
	cv::Mat WriterRetrieval::getFeature() {
		return mFeature;
	}
	/// <summary>
	/// Sets the XML path.
	/// </summary>
	/// <param name="xmlPath">The XML path.</param>
	void WriterRetrieval::setXmlPath(std::string xmlPath) {
		if (xmlPath != "")
			mXmlPath = QString::fromStdString(xmlPath);
	}

	/// <summary>
	/// Draws the keypoinst which were calculated in the compute method
	/// </summary>
	/// <param name="img">The img.</param>
	/// <returns></returns>
	cv::Mat WriterRetrieval::draw(const cv::Mat & img) const {
		cv::Mat imgCopy = img.clone();
		//WriterImage wi = WriterImage();
		//wi.setImage(img);
		//wi.calculateFeatures();
		//wi.filterKeyPoints(config()->vocabulary().minimumSIFTSize(), config()->vocabulary().maximumSIFTSize());

		if(mKeyPoints.empty()) {
			mWarning << "keypoints not calculated .... not drawing";
			return imgCopy;
		}

		QVector<cv::KeyPoint> kp = mKeyPoints;
		for(auto kpItr = kp.begin(); kpItr != kp.end(); kpItr++) {
			kpItr->size *= 1.5 * 4;
		}

#ifdef WITH_XFEATURES2D
		//cv::drawKeypoints(imgCopy, kp.toStdVector(), imgCopy, cv::Scalar::all(-1), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
		cv::drawKeypoints(imgCopy, kp.toStdVector(), imgCopy, cv::Scalar::all(-1), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
#endif

		return imgCopy;
	}
	/// <summary>
	/// To the string.
	/// </summary>
	/// <returns></returns>
	QString WriterRetrieval::toString() const {
		return QString("TODO: write a real toString method");
	}
	/// <summary>
	/// Checks the input. True if a image is set
	/// </summary>
	/// <returns></returns>
	bool WriterRetrieval::checkInput() const {
		return mImg.empty();
	}
}