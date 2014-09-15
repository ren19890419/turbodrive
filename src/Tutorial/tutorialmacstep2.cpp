#include "tutorialmacstep2.h"
#include "ui_tutorialmacstep2.h"
#include <QtGui/QDesktopServices>
#include <QtCore/QUrl>
#include "Util/AppStrings.h"

namespace Drive
{

TutorialMacStep2::TutorialMacStep2(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TutorialMacStep2),
    step(new TutorialStepInterface(this))
{
    ui->setupUi(this);
    step->init(ui->cancelButton, ui->backButton, ui->nextButton);

    connect(ui->googlePlayButton, &QPushButton::clicked, this, &TutorialMacStep2::pushedGooglePlay);
    connect(ui->appStoreButton, &QPushButton::clicked, this, &TutorialMacStep2::pushedAppleAppStore);
}

TutorialMacStep2::~TutorialMacStep2()
{
    delete ui;
}

void TutorialMacStep2::pushedGooglePlay()
{
    QDesktopServices::openUrl(
        QUrl(Strings::getAppString(Strings::GooglePlayURL), QUrl::TolerantMode));
}

void TutorialMacStep2::pushedAppleAppStore()
{
    QDesktopServices::openUrl(
        QUrl(Strings::getAppString(Strings::AppleAppStoreURL), QUrl::TolerantMode));
}

}