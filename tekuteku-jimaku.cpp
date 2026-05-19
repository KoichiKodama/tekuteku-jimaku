#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <filesystem>
#include <boost/format.hpp>

#pragma comment(lib,"RuntimeObject.lib")
#pragma comment(lib,"WindowsApp.lib")

static std::string m_version = "build 2026-05-19";
static std::string m_package_folder; // アプリ本体のフォルダ
static std::string m_local_folder; // アプリが読み書きできるフォルダ
static std::string m_jimaku_html = "jimaku.html";
static std::string m_logfile = "tekuteku-jimaku.log";

std::string k_date_time( int days_off = 0 ) {
	std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	tm l;
	localtime_s(&l,&t);
	if ( days_off != 0 ) {
		l.tm_mday -= days_off;
		t = mktime(&l);
		localtime_s(&l,&t);
	}
	return ( boost::format("%04d-%02d-%02d %02d:%02d:%02d") % (l.tm_year+1900) % (l.tm_mon+1) % (l.tm_mday) % (l.tm_hour) % (l.tm_min) % (l.tm_sec) ).str();
}

bool log( const std::string& message, bool truncate = false ) {
	std::ios_base::openmode mode = ( truncate ? std::ios_base::trunc : std::ios_base::app );
	std::ofstream out(m_logfile,mode);
	if (!out) return false;
	out << k_date_time() << " " << message << "\n";
	return true;
}

bool log( const boost::format& fmt, bool truncate = false ) {
	std::ios_base::openmode mode = ( truncate ? std::ios_base::trunc : std::ios_base::app );
	std::ofstream out(m_logfile,mode);
	if (!out) return false;
	out << k_date_time() << " " << fmt.str() << "\n";
	return true;
}

int main( int argc, char** argv ) {
	winrt::init_apartment();
	try {
		#ifdef MSIX
		// Microsoft Store で公開する ( msix ) 場合、起動フォルダを明示的に指定する。
		// winrt::Windows::ApplicationModel::Package::Current().InstalledLocation() != 実行形式のフォルダ
		TCHAR p[MAX_PATH];
		GetModuleFileName(NULL,p,MAX_PATH);
		std::filesystem::path package_folder = p;
		package_folder.remove_filename();
		std::filesystem::current_path(package_folder);
		m_package_folder = package_folder.string();
		winrt::Windows::Storage::StorageFolder local_folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder(); // LocalState フォルダの取得
		m_local_folder = std::filesystem::path(local_folder.Path().c_str()).string(); // フルパスを文字列として取得
		#else
		m_package_folder = ".";
		m_local_folder = ".";
		#endif

		m_logfile = m_local_folder+"/"+m_logfile;
		log(boost::format("started %s") % m_version);
		log(boost::format("package_folder = %s") % m_package_folder);
		log(boost::format("local_folder = %s") % m_local_folder);
		winrt::Windows::Storage::StorageFolder d = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(winrt::to_hstring(std::filesystem::absolute(std::filesystem::path{m_package_folder}).string())).get();
		winrt::Windows::Storage::StorageFile f = d.GetFileAsync(winrt::to_hstring(m_jimaku_html)).get();
		bool rc = winrt::Windows::System::Launcher::LaunchFileAsync(f).get();
		log(boost::format("launch %s -> %s") % m_jimaku_html % (rc?"ok":"failed"));
	}
	catch ( winrt::hresult_error const& ex ) {
		log(boost::format("exception -> %s") % winrt::to_string(ex.message().c_str()));
	}
	return 0;
}
