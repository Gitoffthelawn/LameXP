///////////////////////////////////////////////////////////////////////////////
// LameXP - Audio Encoder Front-End
// Copyright (C) 2004-2016 LoRd_MuldeR <MuldeR2@GMX.de>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version, but always including the *additional*
// restrictions defined in the "License.txt" file.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// http://www.gnu.org/licenses/gpl-2.0.txt
///////////////////////////////////////////////////////////////////////////////

#include "Filter_Normalize.h"

//Internal
#include "Global.h"

//MUtils
#include <MUtils/Global.h>
#include <MUtils/Exception.h>

//Qt
#include <QDir>
#include <QProcess>
#include <QRegExp>

static double dbToLinear(const double &value)
{
	return pow(10.0, value / 20.0);
}

NormalizeFilter::NormalizeFilter(const int &peakVolume, const bool &dnyAudNorm, const bool &channelsCoupled, const int &filterSize)
:
	m_binary(lamexp_tools_lookup("sox.exe")),
	m_useDynAudNorm(dnyAudNorm),
	m_peakVolume(qMin(-50, qMax(-3200, peakVolume))),
	m_channelsCoupled(channelsCoupled),
	m_filterLength(qBound(3, filterSize + (1 - (filterSize % 2)), 301))
{
	if(m_binary.isEmpty())
	{
		MUTILS_THROW("Error initializing SoX filter. Tool 'sox.exe' is not registred!");
	}
}

NormalizeFilter::~NormalizeFilter(void)
{
}

bool NormalizeFilter::apply(const QString &sourceFile, const QString &outputFile, AudioFileModel_TechInfo *formatInfo, volatile bool *abortFlag)
{
	QProcess process;
	QStringList args;

	args << "-V3" << "-S";
	args << "--temp" << ".";
	args << QDir::toNativeSeparators(sourceFile);
	args << QDir::toNativeSeparators(outputFile);

	if(!m_useDynAudNorm)
	{
		args << "gain";
		args << (m_channelsCoupled ? "-n" : "-nb");
		args << QString().sprintf("%.2f", static_cast<double>(m_peakVolume) / 100.0);
	}
	else
	{
		args << "dynaudnorm";
		args << "-p" << QString().sprintf("%.2f", qBound(0.1, dbToLinear(static_cast<double>(m_peakVolume) / 100.0), 1.0));
		args << "-g" << QString().sprintf("%d", m_filterLength);
		if(!m_channelsCoupled)
		{
			args << "-n";
		}
	}

	if(!startProcess(process, m_binary, args, QFileInfo(outputFile).canonicalPath()))
	{
		return false;
	}

	bool bTimeout = false;
	bool bAborted = false;

	QRegExp regExp("In:(\\d+)(\\.\\d+)*%");

	while(process.state() != QProcess::NotRunning)
	{
		if(*abortFlag)
		{
			process.kill();
			bAborted = true;
			emit messageLogged("\nABORTED BY USER !!!");
			break;
		}
		process.waitForReadyRead(m_processTimeoutInterval);
		if(!process.bytesAvailable() && process.state() == QProcess::Running)
		{
			process.kill();
			qWarning("SoX process timed out <-- killing!");
			emit messageLogged("\nPROCESS TIMEOUT !!!");
			bTimeout = true;
			break;
		}
		while(process.bytesAvailable() > 0)
		{
			QByteArray line = process.readLine();
			QString text = QString::fromUtf8(line.constData()).simplified();
			if(regExp.lastIndexIn(text) >= 0)
			{
				bool ok = false;
				int progress = regExp.cap(1).toInt(&ok);
				if(ok) emit statusUpdated(progress);
			}
			else if(!text.isEmpty())
			{
				emit messageLogged(text);
			}
		}
	}

	process.waitForFinished();
	if(process.state() != QProcess::NotRunning)
	{
		process.kill();
		process.waitForFinished(-1);
	}
	
	emit statusUpdated(100);
	emit messageLogged(QString().sprintf("\nExited with code: 0x%04X", process.exitCode()));

	if(bTimeout || bAborted || process.exitCode() != EXIT_SUCCESS || QFileInfo(outputFile).size() == 0)
	{
		return false;
	}
	
	return true;
}
