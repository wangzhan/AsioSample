// AsioServer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <list>
#include "boost/asio.hpp"
#include "boost/unordered_map.hpp"

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

// server
const int MaxConnectionNum = 65536;
const int MaxRecvSize = 65536;
class Server
{
public:

	Server(io_service& ios, short port) : m_ios(ios), m_acceptor(ios, tcp::endpoint(tcp::v4(), port)), m_cnnIdPool(MaxConnectionNum)
	{
		int current = 0;
		std::generate_n(m_cnnIdPool.begin(), MaxConnectionNum, [&current]{return ++current; });
	}

	~Server()
	{
	}

	void Accept()
	{
		std::cout << "Start Listening " << std::endl;
		std::shared_ptr<RWHandler> handler = CreateHandler();

		m_acceptor.async_accept(handler->GetSocket(), [this, handler](const boost::system::error_code& error)
		{
			std::cout << "Accept: " << GetCurrentThreadId() << std::endl;

			if (error)
			{
				std::cout << error.value() << " " << error.message() << std::endl;
				HandleAcpError(handler, error);
			}

			m_handlers.insert(std::make_pair(handler->GetConnId(), handler));
			std::cout << "current connect count: " << m_handlers.size() << std::endl;

			handler->HandleRead();
			Accept();
		});
	}

private:
	void HandleAcpError(std::shared_ptr <RWHandler> eventHanlder, const boost::system::error_code& error)
	{
		std::cout << "Error，error reason：" << error.value() << error.message() << std::endl;
		//关闭socket，移除读事件处理器
		eventHanlder->CloseSocket();
		StopAccept();
	}

	void StopAccept()
	{
		boost::system::error_code ec;
		m_acceptor.cancel(ec);
		m_acceptor.close(ec);
		m_ios.stop();
	}

	std::shared_ptr<RWHandler> CreateHandler()
	{
		int connId = m_cnnIdPool.front();
		m_cnnIdPool.pop_front();
		std::shared_ptr<RWHandler> handler = std::make_shared<RWHandler>(m_ios);

		handler->SetConnId(connId);

		handler->SetCallBackError([this](int connId)
		{
			RecyclConnid(connId);
		});

		return handler;
	}

	void RecyclConnid(int connId)
	{
		auto it = m_handlers.find(connId);
		if (it != m_handlers.end())
			m_handlers.erase(it);
		std::cout << "current connect count: " << m_handlers.size() << std::endl;
		m_cnnIdPool.push_back(connId);
	}

private:
	io_service& m_ios;
	tcp::acceptor m_acceptor;
	boost::unordered_map<int, std::shared_ptr<RWHandler>> m_handlers;

	std::list<int> m_cnnIdPool;
};

int _tmain(int argc, _TCHAR* argv[])
{
	io_service ios;
	//boost::asio::io_service::work work(ios);
	//std::thread thd([&ios]{ios.run(); }); 

	std::cout << "Main: " << GetCurrentThreadId() << std::endl;

	Server server(ios, 9900);
	server.Accept();
	ios.run();

	//thd.join();

	return 0;
}

