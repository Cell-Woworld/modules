#include "SMS.h"
#include "../proto/SMS.pb.h"
#include <google/protobuf/util/json_util.h>
#include <random>
#ifdef _WIN32
#include <winsock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <codecvt>        // std::codecvt_utf8
#include <regex>
#include <cerrno>

#define TAG "SMS"

namespace SMS
{

#ifndef STATIC_API
	extern "C" PUBLIC_API BioSys::RNA* CreateInstance(BioSys::IBiomolecule* owner)
	{
		return new SMS(owner);
	}
#endif

	SMS::SMS(BioSys::IBiomolecule* owner)
		:RNA(owner, "SMS", this)
	{
		init();
	}

	SMS::~SMS()
	{
	}

	void SMS::OnEvent(const DynaArray& name)
	{
		USING_BIO_NAMESPACE
		const String& _name = name.str();
		switch (hash(_name))
		{
		case "SMS.Send"_hash:
		{
			const int DEFAULT_PORT = 80;
			int _port = ReadValue<int>(_name + ".port");
			_port = _port == 0 ? DEFAULT_PORT : _port;
			int _provider = ReadValue<int>(_name + ".provider.value");
			String _host = ReadValue<String>(_name + ".host");
			String _username = ReadValue<String>(_name + ".username");
			String _password = ReadValue<String>(_name + ".password");
			String _mobile = ReadValue<String>(_name + ".mobile");
			String _message = ReadValue<String>(_name + ".message");
			int _period = ReadValue<int>(_name + ".period");
			bool _need_veri_code = (ReadValue<bool>(_name + ".without_veriCode") == false);
			int _max_length = ReadValue<int>(_name + ".max_length");

			//_message = std::regex_replace(_message, std::regex("\r"), "\\r");
			//_message = std::regex_replace(_message, std::regex("\n"), "\\n");
			if (_period == 0)
				_period = 10;
			int _veri_code = 0;
			if (_need_veri_code == true)
			{
				typedef std::chrono::high_resolution_clock hrclock;
#if defined(_M_X64) || defined(__amd64__)
				unsigned long long _seed = hrclock::now().time_since_epoch().count();
				std::mt19937_64 _generator(_seed);
#else
				unsigned int _seed = (unsigned int)hrclock::now().time_since_epoch().count();
				std::mt19937 _generator(_seed);
#endif
				std::uniform_int_distribution<int> _distribution(1000, 9999);
				_veri_code = _distribution(_generator);
				_message = string_format(_message.c_str(), _veri_code);
			}
			std::cout << "****** Action name: " << _name << ", Request: " << _mobile << ", " << _message << " ******" << std::endl;

			int sockfd;
			int len = 0;
			//const char* host = "api.twsms.com";
			char msg[512], MSGData[512], buf[512];
			struct sockaddr_in address;
			struct hostent* hostinfo;

			memset(&address, 0, sizeof(address));
			hostinfo = gethostbyname(_host.c_str());
			if (!hostinfo) 
			{
				LOG_E(TAG, "no such host: %s\n", _host.c_str());
				WriteValue<String>("SMS.Error.code", "CONNECTION_FAILED");
				WriteValue<String>("SMS.Error.msg", (String)"no such host: "+ _host);
				SendEvent("SMS.Error");
				return;
			}
			address.sin_family = AF_INET;
			address.sin_port = htons(_port);
			address.sin_addr = *(struct in_addr*) * hostinfo->h_addr_list;

			// Create socket
			sockfd = (int)socket(AF_INET, SOCK_STREAM, 0);

			// Connect to server
			if (int _ret_code = connect(sockfd, (struct sockaddr*) & address, sizeof(address)) == -1) 
			{
				LOG_E(TAG, "fail to connect %s, error code: %d\n", _host.c_str(), _ret_code);
				WriteValue<String>("SMS.Error.code", "CONNECTION_FAILED");
				WriteValue<String>("SMS.Error.msg", (String)"fail to connect " + _host + ", error code: " + std::to_string(_ret_code));
				SendEvent("SMS.Error");
				return;
			}

			switch (_provider)
			{
			case Send::TWS:
			{
				// Request string
				len = snprintf(msg, 512,
					"username=%s&password=%s&mobile=%s&message=%s", _username.c_str(), _password.c_str(), _mobile.c_str(), _message.c_str());

				// HTTP request content
				snprintf(MSGData, 512,
					"POST /json/sms_send.php HTTP/1.1\r\n"
					"Host: %s\r\n"
					"Content-Length: %d\r\n"
					"Content-Type: application/x-www-form-urlencoded\r\n"
					"Connection: Close\r\n\r\n"
					"%s\r\n", _host.c_str(), len, msg);

				// Send message
				send(sockfd, MSGData, (int)strlen(MSGData), 0);

				// Response message
				int _recv_size = recv(sockfd, buf, 512, 0);
				String _response_str(buf, _recv_size);
				std::cout << "****** Action name: " << _name << ", Response: " << _response_str << std::endl;
				closeSocket(sockfd);
				size_t _response_json_begin = _response_str.find_first_of('{');
				size_t _response_json_end = _response_str.find_last_of('}');
				String _response_json = _response_str.substr(_response_json_begin, _response_json_end - _response_json_begin + 1);
				Send::MetaData _response;
				using namespace google::protobuf;
				// JSON format
				if (util::JsonStringToMessage(_response_json, &_response).ok())
				{
					if (_response.code() != "00000")
					{
						WriteValue<String>(Send::Result::descriptor()->full_name() + ".veri_code", "");

						LOG_E(TAG, "error code: %s\n", _response.code().c_str());
						switch (std::stoi(_response.code()))
						{
						case 1:
							WriteValue<int>("SMS.Error.code", Error::UNFINISHED_FLOW);
							WriteValue<String>("SMS.Error.msg", "Status not recovered");
							break;
						case 10:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Invalid ID/password format");
							break;
						case 11:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Invalid ID");
							break;
						case 12:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Invalid password");
							break;
						case 20:
							WriteValue<int>("SMS.Error.code", Error::UNAUTHORIZED_ERROR);
							WriteValue<String>("SMS.Error.msg", "Insufficient balance");
							break;
						case 30:
							WriteValue<int>("SMS.Error.code", Error::UNAUTHORIZED_ERROR);
							WriteValue<String>("SMS.Error.msg", "Unauthorized IP address");
							break;
						case 40:
							WriteValue<int>("SMS.Error.code", Error::UNAUTHORIZED_ERROR);
							WriteValue<String>("SMS.Error.msg", "Account disabled");
							break;
						case 50:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: sendtime");
							break;
						case 60:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: expirytime");
							break;
						case 70:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: popup");
							break;
						case 80:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: mo");
							break;
						case 90:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: longsms");
							break;
						case 100:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: phone number");
							break;
						case 110:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Lack of content");
							break;
						case 120:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Longsms doesn't support international call");
							break;
						case 130:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Exceed word limit");
							break;
						case 140:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: drurl");
							break;
						case 150:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Invalid schedule");
							break;
						case 160:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: phone number");
							break;
						case 300:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "msgid not found");
							break;
						case 310:
							WriteValue<int>("SMS.Error.code", Error::UNFINISHED_FLOW);
							WriteValue<String>("SMS.Error.msg", "Message not sent yet");
							break;
						case 400:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "No snumber");
							break;
						case 410:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "No mo information");
							break;
						case 420:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: smsQuery");
							break;
						case 430:
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Format error: moQuery");
							break;
						case 99998:
							WriteValue<int>("SMS.Error.code", Error::UNFINISHED_FLOW);
							WriteValue<String>("SMS.Error.msg", "Not complete process, please send again");
							break;
						case 99999:
							WriteValue<int>("SMS.Error.code", Error::SYSTEM_ERROR);
							WriteValue<String>("SMS.Error.msg", "System error, please inform customer service");
							break;
						}
						SendEvent("SMS.Error");
						return;
					}
					else
					{
						if (_need_veri_code == true)
							WriteValue(Send::Result::descriptor()->full_name() + ".veri_code", _veri_code);
						WriteValue(Send::Result::descriptor()->full_name() + ".msgid", _response.msgid());
						SendEvent(Send::Result::descriptor()->full_name());
					}
				}
				break;
			}
			case Send::CHT:
			{
				struct Send_Msg {
					unsigned	char  msg_type;		//訊息型態
					unsigned	char  msg_coding;	//訊息編碼種類
					unsigned	char  msg_priority;	//訊息優先權
					unsigned	char  msg_country_code; //手機國碼
					unsigned	char  msg_set_len;	//msg_set[] 訊息內容的長度
					unsigned	char  msg_content_len;	//msg_content[]訊息內容的長度
					char  msg_set[100];	//訊息相關資料設定
					char  msg_content[160];	//簡訊內容
				};
				struct Ret_Msg {
					unsigned	char  ret_code;	//回傳訊息代碼
					unsigned	char  ret_coding; 	//訊息編碼種類
					unsigned	char  ret_set_len;	//ret_set[] 訊息內容的長度
					unsigned	char  ret_content_len;	//ret_content[]訊息內容的長度
					char  ret_set[80];	//訊息相關資料
					char  ret_content[160];	//MessageID or 收訊者回傳的簡訊內容
				};
				auto FillMsg = [](char* src, const char* target, int n)
				{
					size_t i, len;
					len = strlen(target);
					for (i = 0; i < len; i++)
						src[n++] = target[i];
					src[n++] = '\0';
					return(n);
				};
				/******* 檢查Username,Pw帳號/密碼 *******/
				struct Send_Msg sendMsg;
				struct Ret_Msg retMsg;
				memset((char*)&sendMsg, 0, sizeof(sendMsg));
				sendMsg.msg_type = 0;
				int iPos = 0, ret = -1;
				iPos = FillMsg(&sendMsg.msg_set[0], _username.c_str(), iPos); //將帳號填入msg_set
				iPos = FillMsg(&sendMsg.msg_set[0], _password.c_str(), iPos);   //將密碼填入msg_set
				sendMsg.msg_set_len = iPos;
				if ((ret = send(sockfd, (char*)&sendMsg, sizeof(sendMsg), 0)) < 0) {
					LOG_E(TAG, "socket sending User/Pwd error");
					closeSocket(sockfd);
					WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
					WriteValue<String>("SMS.Error.msg", "Fail:Socket Sending_User/Pwd_Error!");
					SendEvent("SMS.Error");
					return;
				}
				memset((char*)&retMsg, 0, sizeof(retMsg));
				if ((ret = recv(sockfd, (char*)&retMsg, sizeof(retMsg), 0)) < 0) {
					LOG_E(TAG, "socket receiving User/Pwd error");
					closeSocket(sockfd);
					WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
					WriteValue<String>("SMS.Error.msg", "Fail:Socket Receiving_User/Pwd_Error!");
					SendEvent("SMS.Error");
					return;
				}
				if (retMsg.ret_code != 0) {
					LOG_E(TAG, "Login/Password_Error !\n");
					closeSocket(sockfd);
					WriteValue<int>("SMS.Error.code", Error::UNAUTHORIZED_ERROR);
					WriteValue<String>("SMS.Error.msg", "Fail:Login/Password_Error!");
					SendEvent("SMS.Error");
					return;
				}
				LOG_I(TAG, "CHT_SMS: %s", retMsg.ret_content);

				/******* 傳送訊息(立即傳送) *******/
				char Send_Type[3];              //設定傳送形式 01: 即時傳送 03:限時傳送 
				memset((char*)&sendMsg, 0, sizeof(sendMsg));
				sendMsg.msg_type = 1;   //訊息型態 1 -> 傳送文字簡訊
				sendMsg.msg_coding = 4; //訊息編碼種類 : 4 -> unicode(UTF-8) 編碼
				//sendMsg.msg_coding = 1; //訊息編碼種類 : 1 -> big5 編碼
				strcpy(Send_Type, "01");  //設定傳送形式 01: 即時傳送
				iPos = 0;
				iPos = FillMsg(&sendMsg.msg_set[0], _mobile.c_str(), iPos);//將手機號碼填入msg_set
				iPos = FillMsg(&sendMsg.msg_set[0], Send_Type, iPos);   //將傳送形式填入msg_set
				sendMsg.msg_set_len = iPos;
				int CHT_SMS_MAGIC_NUMBER = 140 - 1;
				if (_max_length > 0)
					CHT_SMS_MAGIC_NUMBER = _max_length;
				//將簡訊內容寫到msg_content
				//memcpy(sendMsg.msg_content, _message.c_str(), _message.size());
				//sendMsg.msg_content_len = (unsigned char)_message.size();
				//for (size_t _start_pos = 0; _start_pos < _message.size();)
				for (size_t _start_pos = 0; _start_pos < _message.size();)
				{
					size_t _end_pos = _start_pos - 1 + (_message.size()-_start_pos < CHT_SMS_MAGIC_NUMBER ? _message.size() - _start_pos : CHT_SMS_MAGIC_NUMBER);
					//size_t _last_lf = (_end_pos == _message.size() - 1) ? _end_pos : _message.find_last_of('\n', _end_pos);
					size_t _last_lf = _message.find_last_of('\n', _end_pos);
					if (_last_lf == String::npos || _last_lf < _start_pos)
					{
						bool _found = false;
						bool _find_delimeter = false;
						for (_last_lf = _end_pos; !_found && _last_lf > _end_pos - 3 && _last_lf > _start_pos;)
						{
							switch (_message[_last_lf] & 0xc0)
							{
							case 0xc0:
								_last_lf--;
								_found = true;
								break;
							case 0x0:
								if (_message[_last_lf] == ' ' || _message[_last_lf] == '-' || _message[_last_lf] == ',' || _message[_last_lf] == '.')
									_found = true;
								else
								{
									_find_delimeter = true;
									_last_lf--;
								}
								break;
							default:
								if (_find_delimeter)
									_found = true;
								else
									_last_lf--;
								break;
							}
						}
					}
					if (_end_pos - _start_pos == CHT_SMS_MAGIC_NUMBER - 1 || _end_pos - _start_pos > sizeof(sendMsg.msg_content)-1)
					{
						if (_last_lf > _start_pos)
						{
							strcpy(sendMsg.msg_content, _message.substr(_start_pos, _last_lf - _start_pos + 1).c_str());
							sendMsg.msg_content_len = (unsigned char)(_last_lf - _start_pos + 1);
							LOG_I(TAG, "LF found. msg_content=%s, size=%d", sendMsg.msg_content, sendMsg.msg_content_len);
							_start_pos = _last_lf + 1;
						}
						else
						{
							LOG_E(TAG, "data is too long error");
							closeSocket(sockfd);
							WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
							WriteValue<String>("SMS.Error.msg", "Fail:Data is too long.");
							SendEvent("SMS.Error");
							return;
						}
					}
					else
					{
						strcpy(sendMsg.msg_content, _message.substr(_start_pos, _end_pos - _start_pos + 1).c_str());
						sendMsg.msg_content_len = (unsigned char)(_end_pos - _start_pos + 1);
						LOG_I(TAG, "LF not found. msg_content=%s, size=%d", sendMsg.msg_content, sendMsg.msg_content_len);
						_start_pos = _end_pos + 1;
					}

					if ((ret = send(sockfd, (char*)&sendMsg, sizeof(sendMsg), 0)) < 0)
					{
						LOG_E(TAG, "socket sending message error");
						closeSocket(sockfd);
						WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
						WriteValue<String>("SMS.Error.msg", "Fail:Socket Sending_Message_Error!");
						SendEvent("SMS.Error");
						return;
					}
					memset((char*)&retMsg, 0, sizeof(retMsg));
					if ((ret = recv(sockfd, (char*)&retMsg, sizeof(retMsg), 0)) < 0) {
						LOG_E(TAG, "socket receiving message error");
						closeSocket(sockfd);
						WriteValue<int>("SMS.Error.code", Error::DATA_ERROR);
						WriteValue<String>("SMS.Error.msg", "Fail:Socket Receiving_Message_Error!");
						SendEvent("SMS.Error");
						return;
					}
					LOG_I(TAG, "CHT_SMS: ret_code[%d],Message Id[%s]\n", retMsg.ret_code, retMsg.ret_content);
					if (retMsg.ret_code != 0)
						break;

					std::this_thread::sleep_for(std::chrono::seconds(_period));
				}
				closeSocket(sockfd);
				break;
			}
			default:
				break;
			}
			break;
		}
		default:
			break;
		}
	}
	void SMS::closeSocket(int sockfd)
	{
#ifdef _WIN32
		closesocket(sockfd);
#else
		close(sockfd);
#endif
	}
}