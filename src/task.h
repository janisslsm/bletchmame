/***************************************************************************

    task.h

    Abstract base class for handling tasks for MAME

***************************************************************************/

#pragma once

#ifndef TASK_H
#define TASK_H

#include <memory>
#include <vector>

#include <QString>
#include <QProcess>

class Preferences;
class MameClient;

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> Task

class Task
{
	friend class MameClient;

public:
	enum class EmuError
	{
		// standard MAME error codes, from MAME main.h
		None = 0,				// no error
		FailedValidity = 1,		// failed validity checks
		MissingFiles = 2,		// missing files
		FatalError = 3,			// some other fatal error
		Device = 4,				// device initialization error (MESS-specific)
		NoSuchGame = 5,			// game was specified but doesn't exist
		InvalidConfig = 6,		// some sort of error in configuration 
		IdentNonRoms = 7,		// identified all non-ROM files
		IdentPartial = 8,		// identified some files but not all
		IdentNone = 9,			// identified no files

		// our error codes
		Invalid = 1000,			// invalid error code
		Killed,					// the process was killed
	};

	typedef std::shared_ptr<Task> ptr;
	virtual ~Task();

protected:
	// called on the main thread to trigger a shutdown (e.g. - BletchMAME is closed)
	virtual void abort() = 0;

	// retrieves the arguments to be used at the command line
	virtual QStringList getArguments(const Preferences &prefs) const = 0;

	// called on a child thread tasked with ownership of a MAME child process
	virtual void process(QProcess &process, QObject &handler) = 0;

	// called on the main thread when the child process has been completed
	virtual void onChildProcessCompleted(EmuError status);

	// called on the main thread when the child process has been killed
	virtual void onChildProcessKilled();
};


#endif // TASK_H
