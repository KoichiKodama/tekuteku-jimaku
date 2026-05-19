#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Foundation.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <filesystem>
#include <boost/format.hpp>

#pragma comment(lib,"RuntimeObject.lib")
#pragma comment(lib,"WindowsApp.lib")
#pragma comment(lib,"user32.lib")

static std::string m_version = "build 2026-05-19";
static std::string m_package_folder; // アプリ本体のフォルダ
static std::string m_local_folder; // アプリが読み書きできるフォルダ
static std::string m_jimaku_html = "jimaku.html";

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

		winrt::Windows::Storage::StorageFolder d = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(winrt::to_hstring(std::filesystem::path{m_package_folder}.string())).get();
		winrt::Windows::Storage::StorageFile f = d.GetFileAsync(winrt::to_hstring(m_jimaku_html)).get();
		bool rc = winrt::Windows::System::Launcher::LaunchFileAsync(f).get();
	}
	catch ( winrt::hresult_error const& ex ) {
		MessageBoxW(NULL,ex.message().c_str(),L"てくてく字幕のエラー",MB_OK);
	}
	return 0;
}
