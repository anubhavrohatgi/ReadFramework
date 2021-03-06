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


#pragma warning(push, 0)	// no warnings from includes
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QImage>
#include <QFileInfo>
#pragma warning(pop)

#include "Utils.h"
#include "Settings.h"
#include "LayoutTest.h"
#include "PreProcessingTest.h"
#include "TableTest.h"

#if defined(_MSC_BUILD) && !defined(QT_NO_DEBUG_OUTPUT) // fixes cmake bug - really release uses subsystem windows, debug and release subsystem console
#pragma comment (linker, "/SUBSYSTEM:CONSOLE")
#else
#pragma comment (linker, "/SUBSYSTEM:WINDOWS")
#endif

int main(int argc, char** argv) {

	// check opencv version
	qInfo().nospace() << "I am using OpenCV " << CV_MAJOR_VERSION << "." << CV_MINOR_VERSION << "." << CV_VERSION_REVISION;

	QCoreApplication::setOrganizationName("TU Wien");
	QCoreApplication::setOrganizationDomain("http://www.caa.tuwien.ac.at/cvl");
	QCoreApplication::setApplicationName("READ Framework");
	rdf::Utils::instance().initFramework();

	QCoreApplication app(argc, (char**)argv);	// enable headless

	// CMD parser --------------------------------------------------------------------
	QCommandLineParser parser;

	parser.setApplicationDescription("READ Framework testing application.");
	parser.addHelpOption();
	parser.addVersionOption();

	// baseline test
	QCommandLineOption baseLineOpt(QStringList() << "baseline", QObject::tr("Test Baseline."));
	parser.addOption(baseLineOpt);

	// baseline test
	QCommandLineOption trainSpOpt(QStringList() << "super-pixel", QObject::tr("Test Super Pixel Training."));
	parser.addOption(trainSpOpt);

	// table test
	QCommandLineOption tableOpt(QStringList() << "table", QObject::tr("Test Table."));
	parser.addOption(tableOpt);

	// pre-processing test
	QCommandLineOption preProcessingOpt(QStringList() << "pre-processing", QObject::tr("Test Pre-Processing."));
	parser.addOption(preProcessingOpt);

	parser.process(*QCoreApplication::instance());
	// CMD parser --------------------------------------------------------------------

	// load settings
	rdf::Config& config = rdf::Config::instance();
	
	// test baseline extraction
	if (parser.isSet(baseLineOpt)) {

		rdf::BaselineTest bt;

		if (!bt.baselineTest())
			return 1;	// fail the test
	}
	// test baseline extraction
	else if (parser.isSet(trainSpOpt)) {

		rdf::SuperPixelTest spt;

		if (!spt.testSuperPixel())
			return 1;	// fail the test

		if (!spt.collectFeatures())
			return 1;	// fail the test

		if (!spt.train())
			return 1;	// fail the test

		if (!spt.eval())
			return 1;	// fail the test

	}
	// test baseline extraction
	else if (parser.isSet(preProcessingOpt)) {

		rdf::PreProcessingTest ppt;

		if (!ppt.binarize())
			return 1;	// fail the test

		if (!ppt.skew())
			return 1;	// fail the test

		if (!ppt.gradient())
			return 1;	// fail the test


	} else if (parser.isSet(tableOpt)) {
		//parser.showHelp();

		rdf::TestConfig tc;
		tc.setXmlPath("ftp://scruffy.caa.tuwien.ac.at/staff/read/test-resources/page/M_Aigen_am_Inn_003-01_0006.xml");
		tc.setImagePath("ftp://scruffy.caa.tuwien.ac.at/staff/read/test-resources/M_Aigen_am_Inn_003-01_0006.jpg");

		//tc.setXmlPath("ftp://scruffy.caa.tuwien.ac.at/staff/read/test-resources/page/RM_Freyung_028_0003.xml");
		//tc.setImagePath("ftp://scruffy.caa.tuwien.ac.at/staff/read/test-resources/RM_Freyung_028_0003.jpg");
		//tc.setTemplateXmlPath("ftp://scruffy.caa.tuwien.ac.at/staff/read/test-resources/page/RM_Freyung_028_0001.xml");

		rdf::TableTest tt(tc);
		
		if (!tt.match())
			return 1;	// fail the test

		qDebug() << "table matched";

	} else 	{
		qInfo() << "Please specify an input image...";
		parser.showHelp();
	}

	// save settings
	config.save();
	return 0;	// thanks
}
