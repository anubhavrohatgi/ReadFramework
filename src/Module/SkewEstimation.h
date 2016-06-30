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

#pragma once

#include "BaseModule.h"


#pragma warning(push, 0)	// no warnings from includes
// Qt Includes
#include <QObject>
#include <QVector>
#include <QVector4D>

#include "opencv2/core/core.hpp"
#pragma warning(pop)

#pragma warning (disable: 4251)	// inlined Qt functions in dll interface

#ifndef DllModuleExport
#ifdef DLL_MODULE_EXPORT
#define DllModuleExport Q_DECL_EXPORT
#else
#define DllModuleExport Q_DECL_IMPORT
#endif
#endif

// Qt defines

namespace rdf {


/// <summary>
/// The class estimates the skew of a document page. The methodology is based on
/// "Skew estimation of natural images based on a salient line detector", Hyung Il Koo and Nam Ik Cho
/// </summary>
/// <seealso cref="Module" />
	class DllModuleExport BaseSkewEstimation : public Module {

	public:
		enum EdgeDirection { HORIZONTAL = 0, VERTICAL };

		BaseSkewEstimation();
		BaseSkewEstimation(const cv::Mat& img, const cv::Mat& mask = cv::Mat());
		void setImages(const cv::Mat& img, const cv::Mat& mask = cv::Mat());

		bool compute();
		double getAngle();
		QVector<QVector4D> getSelectedLines() const;

		bool isEmpty() const override;
		virtual QString toString() const override;

		void setThr(double thr);
		void setmMinLineLength(int ll);
		void setDelta(int d);
		void setEpsilon(int e);
		void setW(int w);
		void setH(int h);
		void setSigma(double s);
		void setFixedThr(bool f);

	protected:
		cv::Mat mSrcImg;									//the input image  either 3 channel or 1 channel [0 255]
		cv::Mat mMask;										//the mask image [0 255]

		cv::Mat separability(const cv::Mat& srcImg, int w, int h, const cv::Mat& mask = cv::Mat());
		cv::Mat edgeMap(const cv::Mat& separability, double thr, EdgeDirection direction = HORIZONTAL, const cv::Mat& mask = cv::Mat()) const;
		QVector<QVector3D> computeWeights(cv::Mat edgeMap, int delta, int epsilon, EdgeDirection direction = HORIZONTAL);
		//according to paper eta should be 0.5
		double skewEst(const QVector<QVector3D>& weights, double imgDiagonal, bool& ok, double eta=0.35);
		bool checkInput() const override;

	private:

		void load(const QSettings& settings) override;
		void save(QSettings& settings) const override;

		double mSkewAngle = 0.0;
		//double mThr = 0.1; //according to the paper
		double mThr = 4.0; //according to the paper
		//double mWeightEps = 0.5;
		int mMinLineLength = 50;
		int minLineProjLength = 50 / 4;
		int mKMax = 7; //according to the paper
		int mNIter = 200; //according to the paper
		int mRotationFactor = 1; //needed if we want to transpose the image in the beginning...
		double mSigma = 0.3; //according to the paper, best for document: 0.5

		int mW = 168; //49 according to the paper, best for document: 60
		int mH = 56; //12 according to the paper, best for document: 28
		int mDelta = 20; //according to the paper
		int mEpsilon = 2; //according to the paper

		bool mFixedThr = true;

		cv::Mat mIntegralImg;
		cv::Mat mIntegralSqdImg;
		
		QVector<QVector4D> mSelectedLines;
		QVector<int> mSelectedLineTypes;

		//void load(const QSettings& settings) override;
		//void save(QSettings& settings) const override;
	};

};