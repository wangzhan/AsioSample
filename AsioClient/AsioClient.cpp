// AsioClient.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <list>
#include <functional>
#include <thread>
#include "boost/asio.hpp"
#include "boost/unordered_map.hpp"
#include "boost/thread.hpp"

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;

// message
class Message
{
public:
	enum { header_length = 4 };
	enum { max_body_length = 512 };

	Message()
		: body_length_(0)
	{
	}

	const char* data() const
	{
		return data_;
	}

	char* data()
	{
		return data_;
	}

	size_t length() const
	{
		return header_length + body_length_;
	}

	const char* body() const
	{
		return data_ + header_length;
	}

	char* body()
	{
		return data_ + header_length;
	}

	size_t body_length() const
	{
		return body_length_;
	}

	void body_length(size_t new_length)
	{
		body_length_ = new_length;
		if (body_length_ > max_body_length)
			body_length_ = max_body_length;
	}

	bool decode_header()
	{
		char header[header_length + 1] = "";
		strncat_s(header, header_length + 1, data_, header_length);
		body_length_ = std::atoi(header);
		if (body_length_ > max_body_length)
		{
			body_length_ = 0;
			return false;
		}
		return true;
	}

	void encode_header()
	{
		char header[header_length + 1] = "";
		sprintf_s(header, header_length + 1, "%4d", body_length_);
		memcpy(data_, header, header_length);
	}

private:
	char data_[header_length + max_body_length];
	std::size_t body_length_;
};

// rwhandler
const int MAX_IP_PACK_SIZE = 65536;
const int HEAD_LEN = 4;
class RWHandler
{
public:

	RWHandler(boost::asio::io_service& ios) : m_sock(ios)
	{
	}

	~RWHandler()
	{
	}

	void HandleRead()
	{
		//三种情况下会返回：1.缓冲区满；2.transfer_at_least为真(收到特定数量字节即返回)；3.有错误发生
		boost::asio::async_read(m_sock, boost::asio::buffer(m_buff), transfer_at_least(HEAD_LEN), [this](const boost::system::error_code& ec, size_t size)
		{
			std::cout << "Read: " << GetCurrentThreadId() << std::endl;

			if (ec != nullptr)
			{
				HandleError(ec);
				return;
			}

			std::cout << m_buff.data() + HEAD_LEN << std::endl;

			HandleRead();
		});
	}

	void HandleWrite(char* data, int len)
	{
		boost::system::error_code ec;
		write(m_sock, buffer(data, len), ec);
		if (ec != nullptr)
			HandleError(ec);
	}

	void HandleWriteAsync(char* data, int len)
	{
		//boost::system::error_code ec;
		//write(m_sock, buffer(data, len), ec);
		//if (ec != nullptr)
		//	HandleError(ec);

		boost::asio::async_write(m_sock, boost::asio::buffer(data, len), transfer_at_least(HEAD_LEN), [this](const boost::system::error_code& ec, size_t size)
		{
			std::cout << "Write: " << GetCurrentThreadId() << std::endl;

			if (ec != nullptr)
			{
				HandleError(ec);
				return;
			}
		});
	}

	tcp::socket& GetSocket()
	{
		return m_sock;
	}

	void CloseSocket()
	{
		boost::system::error_code ec;
		m_sock.shutdown(tcp::socket::shutdown_send, ec);
		m_sock.close(ec);
	}

	void SetConnId(int connId)
	{
		m_connId = connId;
	}

	int GetConnId() const
	{
		return m_connId;
	}

	template<typename F>
	void SetCallBackError(F f)
	{
		m_callbackError = f;
	}

private:
	void HandleError(const boost::system::error_code& ec)
	{
		CloseSocket();
		std::cout << ec.message() << std::endl;
		if (m_callbackError)
			m_callbackError(m_connId);
	}

private:
	tcp::socket m_sock;
	std::array<char, MAX_IP_PACK_SIZE> m_buff;
	int m_connId;
	std::function<void(int)> m_callbackError;
};

// client
class Connector
{
public:

	Connector(io_service& ios, const std::string& strIP, short port) :m_ios(ios), m_socket(ios),
		m_serverAddr(tcp::endpoint(address::from_string(strIP), port)), m_isConnected(false), m_chkThread(nullptr)
	{
		CreateEventHandler(ios);
	}

	~Connector()
	{
	}

	bool Start()
	{
		m_eventHandler->GetSocket().async_connect(m_serverAddr, [this](const boost::system::error_code& error)
		{
			std::cout << "Connect: " << GetCurrentThreadId() << std::endl;

			if (error)
			{
				HandleConnectError(error);
				return;
			}
			std::cout << "connect ok" << std::endl;
			m_isConnected = true;
			m_eventHandler->HandleRead(); //连接成功后发起一个异步读的操作
		});

		boost::this_thread::sleep(boost::posix_time::seconds(1));
		return m_isConnected;
	}

	bool IsConnected() const
	{
		return m_isConnected;
	}

	void Send(char* data, int len)
	{
		if (!m_isConnected)
			return;

		//m_eventHandler->HandleWrite(data, len);
		m_eventHandler->HandleWriteAsync(data, len);
	}

	void AsyncSend(char* data, int len)
	{
		if (!m_isConnected)
			return;

		//m_eventHandler->HandleAsyncWrite(data, len);
	}

private:
	void CreateEventHandler(io_service& ios)
	{
		m_eventHandler = std::make_shared<RWHandler>(ios);
		m_eventHandler->SetCallBackError([this](int connid){HandleRWError(connid); });
	}

	void CheckConnect()
	{
		if (m_chkThread != nullptr)
			return;

		m_chkThread = std::make_shared<std::thread>([this]
		{
			while (true)
			{
				if (!IsConnected())
					Start();

				boost::this_thread::sleep(boost::posix_time::seconds(1));
			}
		});
	}

	void HandleConnectError(const boost::system::error_code& error)
	{
		m_isConnected = false;
		std::cout << error.message() << std::endl;
		m_eventHandler->CloseSocket();
		CheckConnect();
	}

	void HandleRWError(int connid)
	{
		m_isConnected = false;
		CheckConnect();
	}

private:
	io_service& m_ios;
	tcp::socket m_socket;

	tcp::endpoint m_serverAddr; //服务器地址

	std::shared_ptr<RWHandler> m_eventHandler;
	bool m_isConnected;
	std::shared_ptr<std::thread> m_chkThread; //专门检测重连的线程
};

int _tmain(int argc, _TCHAR* argv[])
{
	io_service ios;
	boost::asio::io_service::work work(ios);
	boost::thread thd([&ios]{ios.run(); });

	std::cout << "Main: " << GetCurrentThreadId() << std::endl;

	Connector conn(ios, "127.0.0.1", 9900);
	conn.Start();

	//ios.run();

	if (!conn.IsConnected())
	{
		//Pause();
		return -1;
	}

	const int len = 512;
	char buf[len] = "";

	while (std::cin.getline(buf, sizeof(buf)))
	{
		conn.Send(buf, sizeof(buf));
	}
	return 0;
}

