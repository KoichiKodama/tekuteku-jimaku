#ifdef MSIX
#define BOOST_BEAST_USE_WIN32_FILE 0
#endif

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
#include <thread>
#include <chrono>
#include <filesystem>
#include <regex>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/format.hpp>

#include "json.hpp"
#include "tray.hpp"

#pragma comment(lib,"RuntimeObject.lib")
#pragma comment(lib,"WindowsApp.lib")
#pragma comment(lib,"user32.lib")

namespace beast = boost::beast;
namespace asio = boost::asio;

static std::string m_package_folder; // アプリ本体のフォルダ
static std::string m_local_folder; // アプリが読み書きできるフォルダ
static std::string m_version = "build 2026-05-20";
static std::string m_server_name = "tekuteku-jimaku";
static std::string m_logfile = "tekuteku-jimaku.log";
static asio::ip::port_type m_port = 8080;

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

void truncate_log() {
	if ( std::filesystem::exists(m_logfile) == false ) return;
	std::string fname_old = m_logfile+".old";
	if ( std::filesystem::copy_file(m_logfile,fname_old,std::filesystem::copy_options::overwrite_existing) == false ) return;
	std::ifstream ifs(fname_old);
	std::ofstream ofs(m_logfile,std::ios_base::trunc);
	if (!ifs) return;
	if (!ofs) return;
	std::string date_min = k_date_time(7);	// 7日以前のログを削除する
	std::regex r(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2})");
	std::string s;
	while ( std::getline(ifs,s) ) {
		if ( std::regex_search(s,r) == false ) continue;
		if ( s.compare(0,date_min.length(),date_min) > 0 ) ofs << s << "\n";
	}
	ifs.close();
	ofs.close();
	std::filesystem::remove(fname_old);
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

using socket_t = asio::ip::tcp::socket;
using tcp_stream_t = beast::tcp_stream;
using websocket_stream_t = beast::websocket::stream<beast::tcp_stream>;

beast::string_view mime_type( const std::string& path_ex ) {
	if ( beast::iequals(path_ex,"html")) return "text/html";
	if ( beast::iequals(path_ex,"css" )) return "text/css";
	if ( beast::iequals(path_ex,"txt" )) return "text/plain";
	if ( beast::iequals(path_ex,"js"  )) return "application/javascript";
	if ( beast::iequals(path_ex,"json")) return "application/json";
	if ( beast::iequals(path_ex,"png" )) return "image/png";
	if ( beast::iequals(path_ex,"jpg" )) return "image/jpeg";
	if ( beast::iequals(path_ex,"gif" )) return "image/gif";
	if ( beast::iequals(path_ex,"bmp" )) return "image/bmp";
	if ( beast::iequals(path_ex,"ico" )) return "image/vnd.microsoft.icon";
	if ( beast::iequals(path_ex,"svg" )) return "image/svg+xml";
	return "application/text";
}

struct reply_t {
	tcp_stream_t& m_stream;
	asio::yield_context m_yield;

	reply_t( tcp_stream_t& stream, asio::yield_context yield ) : m_stream(stream),m_yield(yield) {};
	template<class body> void operator()( beast::http::response<body>&& msg ) const {
//		if ( msg.need_eof() ) log("unexpected need_eof() = true in reply_t");
		beast::http::response_serializer<body> s{msg};
		beast::error_code ec;
		beast::http::async_write(m_stream,s,m_yield[ec]);
		if (ec) log(boost::format("reply_t write error %s") % ec.message());
	};
};

class target_parser_t {
public:
	target_parser_t( const std::string url ) { parse(url); };
	void parse( const std::string& url ) {
		m_url = url;
		std::string::size_type i_query = url.find("?");
		std::string::size_type i_anchor = url.find("#");
		m_path = url.substr(0,i_query);
		if ( m_path.back() == '/' ) m_path += "index.html";
		std::string query = ( i_query == std::string::npos ? "" : url.substr(i_query+1,i_anchor) );
		std::string anchor = ( i_anchor == std::string::npos ? "" : url.substr(i_anchor+1) );
		m_path_ex = ( m_path.find_last_of(".") == std::string::npos ? "" : m_path.substr(m_path.find_last_of(".")+1) );

		m_params.clear();
		std::string::size_type ii = 0;
		std::string::size_type jj = query.find_first_of("&"); if ( jj == std::string::npos ) jj = query.size();
		while ( ii < query.size() ) {
			std::string a = query.substr(ii,jj-ii);
			std::string::size_type i = a.find("=");
			m_params.emplace_back(std::make_pair(a.substr(0,i),( i == std::string::npos ? "" : a.substr(i+1) )));
			ii = jj+1;
			jj = query.find_first_of("&",ii);
			if ( jj == std::string::npos ) jj = query.size();
		}
	};
	std::string path() const { return m_path; }
	std::string path_ex() const { return m_path_ex; }
	std::string url() const { return m_url; }
	std::vector<std::string> params() const {
		std::vector<std::string> r;
		std::for_each(m_params.begin(),m_params.end(),[&r]( const auto& c ){ r.emplace_back(c.first+"="+c.second); });
		return r;
	};
	std::string param( const std::string& key ) const {
		auto ii = std::find_if(m_params.begin(),m_params.end(),[key]( const auto& c ){ return c.first == key; });
		return ( ii == m_params.end() ? "" : (*ii).second );
	};

private:
	std::string m_path;
	std::string m_path_ex;
	std::vector<std::pair<std::string,std::string>> m_params;
	std::string m_url;
};

bool is_accessible( const std::string& f ) {
	if ( f == "/jimaku.html" ) return true;
	if ( f == "/toolbox.html" ) return true;
	if ( f == "/tekuteku-jimaku.ico" ) return true;
	if ( f.substr(0,6) == "/tools" ) return true;
	if ( f.substr(0,6) == "/icons" ) return true;
	if ( f == "/env.json" ) return true;
	return false;
}

void exec_http_session( tcp_stream_t& stream, asio::yield_context yield ) {
	beast::error_code ec;
	asio::ip::tcp::socket& socket = beast::get_lowest_layer(stream).socket();
	asio::ip::tcp::socket::endpoint_type ep = socket.remote_endpoint(ec);
	#ifdef USE_SSL
	stream.async_handshake(asio::ssl::stream_base::server,yield[ec]);
	#endif
	beast::flat_buffer buffer;
	beast::http::request<beast::http::string_body> req;

	auto response = [&req]( beast::http::status status, const std::string& msg ){
		beast::http::response<beast::http::string_body> res{status,req.version()};
		res.set(beast::http::field::server,m_server_name);
		res.set(beast::http::field::content_type,"text/html");
		res.keep_alive(req.keep_alive());
		res.body() = msg;
		res.prepare_payload();
		return res;
	};
	auto bad_request = [&response]( const std::string& msg ){ return response(beast::http::status::bad_request,msg); };
	auto not_found = [&response]( const std::string& msg ){ return response(beast::http::status::not_found,msg+"(current="+std::filesystem::current_path().string()+")"); };
	auto internal_error = [&response]( const std::string& msg ){ return response(beast::http::status::internal_server_error,msg); };
	reply_t reply{stream,yield};

	beast::http::async_read(stream,buffer,req,yield[ec]);
	if ( ec == beast::http::error::end_of_stream ) {
		socket.shutdown(asio::ip::tcp::socket::shutdown_send,ec);
		return;
	}
	if (ec) return;

	target_parser_t t(req.target()); // 引数は URL の path のみ
	if ( req.method() != beast::http::verb::get && req.method() != beast::http::verb::head ) return reply(bad_request("unsupported http-method"));
	if ( is_accessible(t.path()) == false ) {
		log(boost::format("exec_http_session rejected '%s'") % t.path());
		return reply(not_found(t.path()));
	}

	std::string file_path = "." + t.path();
	if ( file_path == "./env.json" ) {
		beast::http::response<beast::http::string_body> res{beast::http::status::ok,req.version()};
		res.set(beast::http::field::server,m_server_name);
		res.set(beast::http::field::content_type,mime_type(t.path_ex()));
		res.keep_alive(req.keep_alive());
		nlohmann::json r;
		r["local-folder"] = m_local_folder;
		res.body() = r.dump();
		res.prepare_payload();
		return reply(std::move(res));
	}
	else {
		beast::http::response<beast::http::file_body> res{beast::http::status::ok,req.version()};
		res.set(beast::http::field::server,m_server_name);
		res.keep_alive(req.keep_alive());
		res.body().open(file_path.c_str(),beast::file_mode::scan,ec);
		if ( ec == boost::system::errc::no_such_file_or_directory ) return reply(not_found(t.path()));
		if (ec) return reply(internal_error(t.path()+'/'+ec.message()));
		res.set(beast::http::field::content_type,mime_type(t.path_ex()));
		res.content_length(res.body().size());
		if ( req.method() == beast::http::verb::get ) return reply(std::move(res));
		beast::http::response<beast::http::empty_body> res_x{res};
		return reply(std::move(res_x));
	}
}

void exec_listen( asio::io_context& ioc, asio::ip::tcp::endpoint endpoint, asio::yield_context yield ) {
	boost::system::error_code ec;
	asio::ip::tcp::acceptor acceptor(ioc);
	acceptor.open(endpoint.protocol());
	acceptor.set_option(asio::socket_base::reuse_address(true));
	acceptor.bind(endpoint,ec);
	if (ec) { log(boost::format("exec_listen bind error %s") % ec.message()); }
	acceptor.listen(asio::socket_base::max_listen_connections);
	for (;;) {
		socket_t socket(ioc);
		acceptor.async_accept(socket,yield[ec]);
		if (!ec) { asio::spawn(ioc,std::bind(&exec_http_session,tcp_stream_t(std::move(socket)),std::placeholders::_1)); }
	}
}

asio::io_context ioc_x(6);
static std::thread thread_x{};

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
		truncate_log();
		log(boost::format("started %s on port=%d") % m_version % m_port);
		log(boost::format("package-folder = %s") % m_package_folder);
		log(boost::format("local-folder = %s") % m_local_folder);

		// 同一ポートでの多重起動禁止はトレーの存在確認で行う。
		std::string tray_name = "tekuteku-jimaku";
		if ( tray_exist(tray_name.c_str()) == 1 ) {
			log("stop due to multiple servers");
			MessageBoxW(NULL,L"複数のてくてく字幕サーバを動かせません。",L"てくてく字幕",MB_OK);
			return 0;
		}

		thread_x = std::move(std::thread([]{
			try {
				#ifdef MSIX
				winrt::init_apartment();
				#endif
				auto const ep = asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"),m_port};
				asio::spawn(ioc_x,std::bind(&exec_listen,std::ref(ioc_x),ep,std::placeholders::_1));
				ioc_x.run();
			}
			catch ( boost::system::system_error& e ) { log(boost::format("boost exception in thread_x : %s") % e.what()); }
		}));

		const winrt::hstring uri = winrt::to_hstring((boost::format("http://localhost:%d/jimaku.html") % m_port).str());
		winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(uri));
		if ( tray_init("tekuteku-jimaku","tekuteku-jimaku.ico") == 0 ) { while ( tray_loop(1) == 0 ) {} }
		ioc_x.stop(); thread_x.join();
		log("service stopped");
		return 0;
	}
	catch ( winrt::hresult_error const& ex ) { MessageBoxW(NULL,ex.message().c_str(),L"てくてく字幕サーバのエラー",MB_OK); }
	catch ( boost::system::system_error& e ) { log(boost::format("boost exception : %s") % e.what()); }
	catch ( std::exception& e ) { log(boost::format("exception : %s") % e.what()); }
	catch ( ... ) { log("unknown exception"); }
	return 1;
}
