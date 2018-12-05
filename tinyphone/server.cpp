#include "stdafx.h"

#include "server.h"
#include "utils.h"
#include "phone.h"
#include "net.h"
#include <unordered_set>
#include <mutex>
#include "channel.h"
#include "json.h"

using namespace std;
using namespace pj;
using json = nlohmann::json;

#define DEFAULT_HTTP_SERVER_ERROR_REPONSE  {{ "message", "Something went Wrong :(" }}

namespace tp {
	struct response : crow::response {
		response(int code, const nlohmann::json& _body) : crow::response{ code,  _body.dump() } {
			add_header("Access-Control-Allow-Origin", "*");
			add_header("Access-Control-Allow-Headers", "Content-Type");
			add_header("Content-Type", "application/json");
		}
	};
}


void TinyPhoneHttpServer::Start() {

	pj_thread_auto_register();
	channel<std::string> updates;

	std::cout << "Starting the TinyPhone HTTP API" << std::endl;
	TinyPhone phone(endpoint);
	phone.SetCodecs();	
	phone.ConfigureAudioDevices();
	phone.CreateEventStream(&updates);

	crow::App<TinyPhoneMiddleware> app;
	std::mutex mtx;;
	//app.get_middleware<TinyPhoneMiddleware>().setMessage("tinyphone");
	app.loglevel(crow::LogLevel::Info);
	//crow::logger::setHandler(std::make_shared<TinyPhoneHTTPLogHandler>());	
	int http_port = 6060;

	/* Define HTTP Endpoints */
	CROW_ROUTE(app, "/")([]() {
		json response = {
			{ "message", "Hello World" },
		};
		return tp::response(200, response);
	});

	std::unordered_set<crow::websocket::connection*> subscribers;

	CROW_ROUTE(app, "/events")
		.websocket()
		.onopen([&](crow::websocket::connection& conn) {
		CROW_LOG_INFO << "new websocket connection" ;
		std::lock_guard<std::mutex> _(mtx);
		subscribers.insert(&conn);
		json message = {
			{ "message", "welcome" },
			{ "subcription", "created" }
		};
		conn.send_text(message.dump());
	})
		.onclose([&](crow::websocket::connection& conn, const std::string& reason) {
		CROW_LOG_INFO << "websocket connection closed: %s" << reason;
		std::lock_guard<std::mutex> _(mtx);
		subscribers.erase(&conn);
	})
		.onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
		json message = {
			{ "message", "nothing here" },
		};
		conn.send_text(message.dump());
	});

	std::thread thread_object([&updates,&subscribers]() {
		std::string data;
		while (!updates.is_closed()) {
			if (updates.pop(data, true)) {
				for(auto u : subscribers)
					u->send_text(data);
			}
		}
		CROW_LOG_INFO << "Exiting Channel Relayer Thread ";
		//for (auto u : subscribers)
		//	u->close();
	});


	CROW_ROUTE(app, "/devices")
		.methods("GET"_method)
		([&phone]() {
		try {				
			pj_thread_auto_register();
			AudDevManager& audio_manager = Endpoint::instance().audDevManager();
			audio_manager.refreshDevs();
			AudioDevInfoVector devices = audio_manager.enumDev();
			json response = {
				{ "message",  "Audio Devices" },
				{ "count", devices.size() },
				{ "devices",{} },
			};
			int dev_idx = 0;
			BOOST_FOREACH(AudioDevInfo* info, devices) {
				json dev_info = {
					{ "name",info->name },
					{ "driver",info->driver },
					{ "id", dev_idx },
					{ "inputCount" ,  info->inputCount },
					{ "outputCount" ,  info->outputCount },
				};
				response["devices"].push_back(dev_info);
				dev_idx++;
			}
			return tp::response(200, response);
		}
		catch (...) {
			return tp::response(500, DEFAULT_HTTP_SERVER_ERROR_REPONSE);
		}
	});
	
	CROW_ROUTE(app, "/login")
		.methods("POST"_method)
		([&phone](const crow::request& req) {
		try {
			auto x = crow::json::load(req.body);
			if (!x) {
				return tp::response(400, {
					{ "message", "Bad Request" },
				});
			}

			if (phone.Accounts().size() >= PJSUA_MAX_ACC ) {
				return tp::response(429, {
					{ "message", "Max Account Allowed Reached." },
				});
			}

			string username = x["username"].s();
			string domain = x["domain"].s();
			string password = x["password"].s();
			string account_name = SIP_ACCOUNT_NAME(username, domain);

			pj_thread_auto_register();
			CROW_LOG_INFO << "Registering account " << account_name;
			if (phone.AddAccount(username, domain, password)) {
				return tp::response(200, {
					{ "message", "Account added succesfully" },
					{ "account_name", account_name },
				});
			}
			else {
				return tp::response(400, {
					{ "message", "Account already exits" },
					{ "account_name", account_name },
				});
			}
		}
		catch (pj::Error& e) {
			CROW_LOG_ERROR << "Exception catched : " << e.reason;
			return tp::response(500, DEFAULT_HTTP_SERVER_ERROR_REPONSE);
		}
	});

	CROW_ROUTE(app, "/accounts")
		.methods("GET"_method)
		([&phone]() {
		try {
			json response = {
				{ "message",  "Accounts" },
				{ "accounts", json::array() },
			};
			pj_thread_auto_register();
			BOOST_FOREACH(SIPAccount* account, phone.Accounts()) {
				json account_data = {
					{ "id" , account->getId() },
					{"name" , account->Name()},
					{"active" , account->getInfo().regIsActive },
					{"status" , account->getInfo().regStatusText }
				};
				response["accounts"].push_back(account_data);
			}
			return tp::response(200, response);
		}
		catch (...) {
			return tp::response(500, DEFAULT_HTTP_SERVER_ERROR_REPONSE);
		}
	});

	CROW_ROUTE(app, "/dial")
		.methods("POST"_method)
		([&phone](const crow::request& req) {

		std::string dial_uri =  req.body;

		pj_thread_auto_register();

		SIPAccount* account = phone.PrimaryAccount();
		if (account == NULL) {
			return tp::response(400, {
				{ "message", "No Account Registed/Active Yet" },
			});
		}

		if (account->calls.size() >= PJSUA_MAX_CALLS ) {
			return tp::response(429, {
				{ "message", "Max Concurrent Calls Reached. Please try again later." },
			});
		}

		auto sip_uri = tp::GetSIPURI(dial_uri, account->domain);
		auto sip_dial_uri = (char *)sip_uri.c_str();

		CROW_LOG_INFO << "Dial Request to " << sip_dial_uri;
		try {
			SIPCall *call = phone.MakeCall(sip_dial_uri, account);
			string account_name = call->getAccount()->Name();
			json response = {
				{ "message", "Dialling"},
				{ "call_id", call->getId() },
				{ "sid", call->getInfo().callIdString },
				{ "party", sip_dial_uri },
				{ "account", account_name }
			};
			return tp::response(202, response);
		}
		catch (pj::Error& e) {
			CROW_LOG_ERROR << "Exception catched : " << e.reason;
			return tp::response(500, DEFAULT_HTTP_SERVER_ERROR_REPONSE);
		}

	});

	CROW_ROUTE(app, "/calls")
		.methods("GET"_method)
		([&phone]() {
		try {
			pj_thread_auto_register();
			json response = {
				{ "message",  "Current Calls" },
				{ "count",  phone.Calls().size() },
				{ "data", json::array() },
			};

			BOOST_FOREACH(SIPCall* call, phone.Calls()) {
				if (call->getId() >= 0) {
					auto ci = call->getInfo();
					json callinfo = {
						{ "id", ci.id },
						{ "sid", ci.callIdString },
						{ "party" , ci.remoteUri },
						{ "state" ,  ci.stateText },
						{ "duration",  ci.totalDuration.sec },
						{ "hold" ,  ("" + call->HoldState()) }
					};
					response["data"].push_back(callinfo);
				}
			}
			return tp::response(200, response);
		}
		catch (...) {
			return tp::response(500, DEFAULT_HTTP_SERVER_ERROR_REPONSE);
		}
	});

	CROW_ROUTE(app, "/hold/<int>")
		.methods("PUT"_method, "DELETE"_method)
		([&phone](const crow::request& req, int call_id) {

		pj_thread_auto_register();

		SIPCall* call = phone.CallById(call_id);
		if (call == NULL) {
			return tp::response(400, {
				{ "message", "Call Not Found" },
				{ "call_id" , call_id }
			});
		}
		else {
			json response;
			bool status;
			switch (req.method) {
			case crow::HTTPMethod::Put:
				status = call->HoldCall();
				response = {
					{ "message",  "Hold Triggered" },
					{ "call_id" , call_id },
					{ "status" , status }
				};
				break;
			case crow::HTTPMethod::Delete:
				status = call->UnHoldCall();
				response = {
					{ "message",  "UnHold Triggered" },
					{ "call_id" , call_id },
					{ "status" , status }
				};
				break;
			}
			return tp::response(202, response);
		}
	});

	CROW_ROUTE(app, "/hangup/<int>")
		.methods("POST"_method)
		([&phone](int call_id) {
		pj_thread_auto_register();

		SIPCall* call = phone.CallById(call_id);
		if (call == NULL) {
			return tp::response(400, {
				{ "message", "Call Not Found" },
				{"call_id" , call_id}
			});
		}
		else {
			phone.Hangup(call);
			json response = {
				{ "message",  "Hangup Triggered" },
				{ "call_id" , call_id }
			};
			return tp::response(202, response);
		}
	});


	CROW_ROUTE(app, "/hangup_all")
		.methods("POST"_method)
		([&phone]() {
		pj_thread_auto_register();
		phone.HangupAllCalls();
		json response = {
			{ "message",  "Hangup All Calls Triggered" },
		};
		return tp::response(202, response);
	});

	CROW_ROUTE(app, "/logout")
		.methods("POST"_method)
		([&phone]() {
		try {
			pj_thread_auto_register();
			CROW_LOG_INFO << "Atempting logout of TinyPhone";
			phone.Logout();
			json response = {
				{ "message",  "Logged Out" },
			};
			return tp::response(200, response);
		}
		catch (...) {
			return tp::response(500, DEFAULT_HTTP_SERVER_ERROR_REPONSE);
		}
	});

	CROW_ROUTE(app, "/exit")
		.methods("POST"_method)
		([&app](const crow::request& req) {
		CROW_LOG_INFO << "Shutdown Request from client: " << req.body;
		app.stop();
		json response = {
			{ "message",  "Server Shutdown Triggered" },
		};
		return tp::response(200, response);
	});


	if (is_tcp_port_in_use(http_port)) {
		DisplayError(("HTTP Port " + to_string(http_port) + " already in use! \nIs another instance running?"));
		endpoint->libDestroy();
		exit(1);
	}

	app.port(http_port)
		.multithreaded()
		.run();

	CROW_LOG_INFO << "Terminating current running call(s) if any";

	phone.HangupAllCalls();
	endpoint->libDestroy();

	CROW_LOG_INFO << "Server has been shutdown... Will Exit now...." ;

	updates.close();
	thread_object.join();
	
	CROW_LOG_INFO << "Shutdown Complete..";
}