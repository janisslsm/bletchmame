/***************************************************************************

    mamerunner.cpp

    Testing infrastructure

***************************************************************************/

#include <stdexcept>
#include <QProcess>
#include <QThread>
#include "mamerunner.h"
#include "mameworkercontroller.h"


//-------------------------------------------------
//  getAnsiColorCodeForChatterType
//-------------------------------------------------

static const char *getAnsiColorCodeForChatterType(MameWorkerController::ChatterType chatterType)
{
    const char *result;
    switch (chatterType)
    {
    case MameWorkerController::ChatterType::Command:
        result = "\x1B[36m";
        break;
    case MameWorkerController::ChatterType::GoodResponse:
        result = "\x1B[92m";
        break;
    case MameWorkerController::ChatterType::ErrorResponse:
        result = "\x1B[91m";
        break;
    default:
        throw false;
    }
    return result;
}


//-------------------------------------------------
//  chatter
//-------------------------------------------------

static void chatter(MameWorkerController::ChatterType chatterType, const QString &text)
{
    printf("%s%s\n",
        getAnsiColorCodeForChatterType(chatterType),
        text.trimmed().toLocal8Bit().constData());
}


//-------------------------------------------------
//  receiveResponseEnsureSuccess
//-------------------------------------------------

static MameWorkerController::Response receiveResponseEnsureSuccess(MameWorkerController &controller)
{
    MameWorkerController::Response response = controller.receiveResponse();
    if (response.m_type != MameWorkerController::Response::Type::Ok)
        throw std::logic_error("Received invalid response from MAME");
    return response;
}


//-------------------------------------------------
//  runAndExcerciseMame
//-------------------------------------------------

void runAndExcerciseMame(int argc, char *argv[])
{
    // identify the program
    QString program = argv[0];

    // identify the arguments
    QStringList arguments;
    for (int i = 1; i < argc; i++)
        arguments << argv[i];

    // start the process
    QProcess process;
    process.setReadChannel(QProcess::StandardOutput);
    process.start(program, arguments);

    // start controlling MAME
    MameWorkerController controller(process, chatter);

    // read the initial response
    receiveResponseEnsureSuccess(controller);

    // turn off throttling
    controller.issueCommand("THROTTLED 0\n");
    receiveResponseEnsureSuccess(controller);

    // resume!
    controller.issueCommand("RESUME\n");
    receiveResponseEnsureSuccess(controller);

    // sleep and ping
    QThread::sleep(5);
    controller.issueCommand("PING\n");
    receiveResponseEnsureSuccess(controller);

    // sleep and exit
    QThread::sleep(5);
    controller.issueCommand("EXIT\n");
    receiveResponseEnsureSuccess(controller);

    // wait for exit
    if (!process.waitForFinished())
        throw std::logic_error("waitForFinished() returned false");
}
