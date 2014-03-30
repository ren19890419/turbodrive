#include "LoginController.h"

#include "AppController.h"
#include "LoginUI/LoginWidget.h"
#include "SettingsUI/SettingsWidget.h"
#include "QsLog/QsLog.h"
#include "Settings/settings.h"
#include "Settings/runtimesettings.h"

#include "APIClient/ApiTypes.h"
#include "APIClient/AuthenticationService.h"
#include "APIClient/DashboardService.h"
#include "APIClient/ProfileService.h"
#include "Network/SimpleDownloader.h"

#include <QtCore/QCoreApplication>
#include <QtWidgets/QMessageBox>

#include <QDir>

namespace
{

bool needSyncDirClear()
{
	const auto dirPath = Drive::Settings::instance().get(Drive::Settings::folderPath).toString();
	const auto syncDir = QDir(dirPath);
	const auto entries = syncDir.entryList(QDir::AllEntries
										| QDir::System
										| QDir::NoDotAndDotDot);

	const auto currentLogin =
			Drive::RuntimeSettings::instance().get(Drive::RuntimeSettings::login);
	const auto lastLogin =
			Drive::Settings::instance().get(Drive::Settings::email);

	const auto result = currentLogin != lastLogin && !entries.isEmpty();

	QLOG_DEBUG()
			<< "needSyncDirClear [" << dirPath
			<< ", " << lastLogin.toString()
			<< ", " << currentLogin.toString()
			<< "]: " << result;
	return result;
}

bool syncDirClearingConfirmed()
{
	const auto dirPath = Drive::Settings::instance().get(Drive::Settings::folderPath).toString();
	const auto result = true;

	QLOG_DEBUG() << "syncDirClearingConfirmed [" << dirPath << "]: " << result;
	return result;
}

bool clearSyncDir()
{
	const auto dirPath = Drive::Settings::instance().get(Drive::Settings::folderPath).toString();
	auto syncDir = QDir(dirPath);

	const auto result = syncDir.removeRecursively();
	Drive::AppController::instance().createFolder();

	QLOG_DEBUG() << "clearSyncDir [" << dirPath << "]: " << result;
	//return result;
	return true;
}

}

namespace Drive
{

LoginWidget* LoginController::loginWidget = 0;

LoginController& LoginController::instance()
{
	static LoginController myself;
	return myself;
}

LoginController::LoginController(QObject *parent)
	: QObject(parent)
{
	connect(this, SIGNAL(loginFinished()), this, SLOT(onLoginFinished()));
}

LoginController::~LoginController()
{
	if (loginWidget)
		delete loginWidget;
}

void LoginController::showLoginFormOrLogin()
{
	const auto email = Settings::instance().get(Settings::email).toString();
	const auto password = Settings::instance().get(Settings::password).toString();
	const auto autoLogin = Settings::instance().get(Settings::autoLogin).toBool();
	if (autoLogin && !email.isEmpty() && !password.isEmpty())
	{
		RuntimeSettings::instance().set(RuntimeSettings::login, email);
		RuntimeSettings::instance().set(RuntimeSettings::password, password);
		login();
	}
	else
	{
		showLoginForm();
	}
}

void LoginController::showLoginForm()
{
	if (!loginWidget)
	{
		loginWidget = new LoginWidget();

		connect(loginWidget, SIGNAL(loginRequest()),
			this, SLOT(login()));

		connect(loginWidget, SIGNAL(passwordResetRequest(QString)),
			this, SLOT(passwordReset(QString)));


		RegisterLinkResourceRef regLink = RegisterLinkResource::create();
		connect(regLink.data(), SIGNAL(linkReceived(QString)),
			loginWidget, SLOT(setRegisterLink(QString)));

		regLink->requestRegisterLink();
	}

	loginWidget->show();
}

void LoginController::login()
{
	QLOG_INFO() << "LoginController::login()";

	AppController::instance().setState(Authorizing);

	if (loginWidget)
	{
		loginWidget->enableControls(false);
		QCoreApplication::processEvents();
	}

	AuthRestResourceRef authResource = AuthRestResource::create();
	connect(authResource.data(), SIGNAL(loginSucceeded(QString)),
		this,  SLOT(onLoginSucceeded(QString)));

	connect(authResource.data(), SIGNAL(loginFailed(QString)),
		this,  SLOT(onLoginFailed(QString)));

	AuthRestResource::Input inputData;
	inputData.username = RuntimeSettings::instance().get(RuntimeSettings::login).toString();
	inputData.password = RuntimeSettings::instance().get(RuntimeSettings::password).toString();

	authResource->login(inputData);
}

void LoginController::passwordReset(const QString& email)
{
	QLOG_INFO() << "LoginController::passwordReset()";

	if (loginWidget)
	{
		loginWidget->enableControls(false);
		QCoreApplication::processEvents();
	}

	PasswordResetResourceRef passwordResetResource =
		PasswordResetResource::create();

	connect(passwordResetResource.data(), SIGNAL(resetSuccessfully()),
		this,  SLOT(onPasswordResetSucceeded()));

	connect(passwordResetResource.data(), SIGNAL(resetFailed(QString)),
		this,  SLOT(onPasswordResetFailed(QString)));

	passwordResetResource->resetPassword(email);
}

void LoginController::closeAll()
{
	if (loginWidget)
	{
		loginWidget->close();
		delete loginWidget;
		loginWidget = 0;
	}
}

void LoginController::requestUserData()
{
	QLOG_INFO() << "LoginController::requestUserData()";

	ProfileRestResourceRef userResource = ProfileRestResource::create();

	connect(userResource.data(), SIGNAL(profileDataReceived(QJsonObject)),
		this,  SLOT(onProfileDataReceived(QJsonObject)));

	connect(userResource.data(), SIGNAL(profileDataError()),
		this,  SLOT(onProfileDataError()));

	userResource->requestProfileData();
}

void LoginController::onLoginSucceeded(const QString& token)
{
	if (needSyncDirClear())
	{
		if (!syncDirClearingConfirmed())
		{
			onLoginFailed(tr("Login cancelled by user."));
			return;
		}
		else if (!clearSyncDir())
		{
			onLoginFailed(tr("Directory clearing failed."));
			return;
		}
	}

	Settings::instance().set(Settings::email,
		RuntimeSettings::instance().get(RuntimeSettings::login), Settings::RealSetting);

	Settings::instance().set(Settings::password,
		RuntimeSettings::instance().get(RuntimeSettings::password), Settings::RealSetting);

	RuntimeSettings::instance().remove(RuntimeSettings::login);
	RuntimeSettings::instance().remove(RuntimeSettings::password);

	AppController::instance().setAuthToken(token);
	requestUserData();
}

void LoginController::onLoginFailed(const QString& error)
{
	RuntimeSettings::instance().remove(RuntimeSettings::login);
	RuntimeSettings::instance().remove(RuntimeSettings::password);

	showLoginForm();

	loginWidget->enableControls();
	loginWidget->focusOnEmail();
	loginWidget->setError(error);

	AppController::instance().setState(NotAuthorized);
}

void LoginController::onPasswordResetSucceeded()
{
	if (loginWidget)
	{
		loginWidget->enableControls(true);
	}

	QWidget *w = loginWidget ? loginWidget : 0;

	QMessageBox::information(w, tr("Please check your email"),
		tr("We've sent you an email with further instructions about password reset."));
}

void LoginController::onPasswordResetFailed(const QString& error)
{
	if (loginWidget)
	{
		loginWidget->enableControls(true);
	}

	QWidget *w = loginWidget ? loginWidget : 0;
	QMessageBox::warning(w, tr("Password reset failed"), error);
}

void LoginController::onProfileDataReceived(const QJsonObject& data)
{
	ProfileData profileData = ProfileData::fromJson(data);

	//userData.log();

	if (!profileData.isValid())
	{
		onProfileDataError();
		return;
	}

//	QString userName = QString("%1 %2")
//		.arg(map.value("first_name", QVariant()).toString())
//		.arg(map.value("last_name", QVariant()).toString())
//		.trimmed();

	AppController::instance().setProfileData(profileData);
	emit loginFinished();

	QUrl url(profileData.avatarUrl);
	if (url.isValid())
	{
		QString newUrl = url.toString();
		int dotPos = newUrl.lastIndexOf(".");
		if (dotPos == -1)
		{
			QLOG_ERROR() << "Bad avatar image URL: " << profileData.avatarUrl;
			return;
		}

		newUrl = QString("%1%2%3")
			.arg(newUrl.left(dotPos))
			.arg("@avatar_desktop")
			.arg(newUrl.right(newUrl.size() - dotPos));

		SimpleDownloader *d = new SimpleDownloader(QUrl(newUrl),
			SimpleDownloader::Pixmap, this);

		QLOG_TRACE() << "Downloading avatar from: " << newUrl;

		connect(d, SIGNAL(finished(QPixmap)),
			this, SLOT(onAvatarDownloaded(QPixmap)));
	}
	else
	{
		QLOG_INFO() << "No avatar set. URL is: " << profileData.avatarUrl;
	}
}

void LoginController::onProfileDataError()
{
	onLoginFailed(tr("Login failed: can't obtain user details"));
}

void LoginController::onAvatarDownloaded(const QPixmap& pixmap)
{
	ProfileData profileData = AppController::instance().profileData();
	profileData.avatar = pixmap;
	AppController::instance().setProfileData(profileData);
	// TODO: delete downloader
}

void LoginController::onLoginFinished()
{
	QLOG_TRACE() << "Login finished ok, closing the window";
	closeAll();
	QCoreApplication::processEvents();
}

}
