/*
 * AsyncIOServer.cpp
 *
 *  Created on: 2016年9月13日
 *      Author: Max.Chiu
 */

#include "AsyncIOServer.h"

class RecvRunnable : public KRunnable {
public:
	RecvRunnable(AsyncIOServer *container) {
		mContainer = container;
	}
	virtual ~RecvRunnable() {
		mContainer = NULL;
	}
protected:
	void onRun() {
		mContainer->RecvHandleThread();
	}
private:
	AsyncIOServer *mContainer;
};

AsyncIOServer::AsyncIOServer()
:mServerMutex(KMutex::MutexType_Recursive)
{
	// TODO Auto-generated constructor stub
	mRunning = false;
	mpAsyncIOServerCallback = NULL;

	miMaxConnection = 1000;
	miConnection = 0;

	mThreadCount = 4;
	mpHandleThreads = NULL;
	mpRecvRunnable = new RecvRunnable(this);

	mTcpServer.SetTcpServerCallback(this);
}

AsyncIOServer::~AsyncIOServer() {
	// TODO Auto-generated destructor stub
	Stop();

	if( mpHandleThreads ) {
		delete[] mpHandleThreads;
		mpHandleThreads = NULL;
	}

	if( mpRecvRunnable ) {
		delete mpRecvRunnable;
		mpRecvRunnable = NULL;
	}
}

void AsyncIOServer::SetAsyncIOServerCallback(AsyncIOServerCallback* callback) {
	mpAsyncIOServerCallback = callback;
}

bool AsyncIOServer::Start(
		unsigned port,
		int maxConnection,
		int iThreadCount,
		const char *ip
		) {
	bool bFlag = false;

	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"AsyncIOServer::Start( "
			"[Start], "
			"port : %u, "
			"maxConnection : %d, "
			"iThreadCount : %d, "
			"ip : %s "
			")",
			port,
			maxConnection,
			iThreadCount,
			ip
			);

	mServerMutex.lock();
	if( mRunning ) {
		Stop();
	}
	mRunning = true;
	mThreadCount = iThreadCount;
	miMaxConnection = maxConnection;
	miConnection = 0;

	// 创建内存
	for(int i = 0; i < (int)miMaxConnection; i++) {
		Client* client = Client::Create();
		mClientIdleList.PushBack(client);
	}

	// 创建处理线程
	mpHandleThreads = new KThread*[iThreadCount];
	for(int i = 0; i < iThreadCount; i++) {
		mpHandleThreads[i] = new KThread(mpRecvRunnable);
		mpHandleThreads[i]->start();
	}

	// 开始监听socket
	bFlag = mTcpServer.Start(port, maxConnection, ip);

	if( bFlag ) {
		LogManager::GetLogManager()->Log(
				LOG_MSG,
				"AsyncIOServer::Start( "
				"[OK], "
				"port : %d, "
				"maxConnection : %d, "
				"iThreadCount : %d, "
				"ip : %s "
				")",
				port,
				maxConnection,
				iThreadCount,
				ip
				);
	} else {
		LogManager::GetLogManager()->Log(
				LOG_ERR_SYS,
				"AsyncIOServer::Start( "
				"[Fail], "
				"port : %d, "
				"maxConnection : %d, "
				"iThreadCount : %d, "
				"ip : %s "
				")",
				port,
				maxConnection,
				iThreadCount,
				ip
				);
		Stop();
	}
	mServerMutex.unlock();

	return bFlag;
}

void AsyncIOServer::Stop() {
	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"AsyncIOServer::Stop( "
			")"
			);

	mServerMutex.lock();

	if( mRunning ) {
		mRunning = false;

		// 停止监听socket
		mTcpServer.Stop();

		// 停止处理线程
		if( mpHandleThreads != NULL ) {
			for(int i = 0; i < mThreadCount; i++) {
				if( mpHandleThreads[i] != NULL ) {
					mpHandleThreads[i]->stop();
					delete mpHandleThreads[i];
					mpHandleThreads[i] = NULL;
				}
			}
		}

		// 销毁处理队列
		Client* client = NULL;
		while( NULL != ( client = mClientHandleList.PopFront() ) ) {
		}

		// 销毁空闲队列
		while( NULL != ( client = mClientIdleList.PopFront() ) ) {
			delete client;
		}

		miConnection = 0;
	}

	mServerMutex.unlock();

	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"AsyncIOServer::Stop( "
			"[OK] "
			")"
			);
}

bool AsyncIOServer::IsRunning() {
	return mRunning;
}

bool AsyncIOServer::Send(Client* client, const char* buf, int &len) {
	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"AsyncIOServer::Send( "
			"client : %p, "
			"len : %d, "
			"buf :\n%s\n"
			")",
			client,
			len,
			buf
			);
	bool bFlag = false;

	if( client ) {
		client->clientMutex.lock();
		if( !client->disconnected ) {
			bFlag = mTcpServer.Send(client->socket, buf, len);
			if( !bFlag ) {
				Disconnect(client);
			}
		}
		client->clientMutex.unlock();
	}

	return bFlag;
}

void AsyncIOServer::Disconnect(Client* client) {
	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"AsyncIOServer::Disconnect( "
			"client : %p, "
			"ip : %s, "
			"port : %u "
			")",
			client,
			client->socket->ip.c_str(),
			client->socket->port
			);
	if( client ) {
		client->clientMutex.lock();
		mTcpServer.Disconnect(client->socket);
		client->clientMutex.unlock();
	}
}

unsigned int AsyncIOServer::GetHandleCount() {
	return mClientHandleList.Size();
}

bool AsyncIOServer::OnAccept(Socket* socket) {
	bool bFlag = false;

	// 创建新连接
	Client* client = NULL;

	if( (client = mClientIdleList.PopFront()) == NULL ) {
		// 申请额外内存
		client = Client::Create();

		LogManager::GetLogManager()->Log(
				LOG_WARNING,
				"AsyncIOServer::OnAccept( "
				"[Not enough client, new more], "
				"client : %p, "
				"miConnection : %u "
				")",
				client,
				miConnection
				);

		bFlag = true;

		// 超过一半连接数目, 释放CPU, 让处理线程处理
		usleep(200 * 1000);

	} else {
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"AsyncIOServer::OnAccept( "
				"[Get client from idle list], "
				"client : %p, "
				"miConnection : %u "
				")",
				client,
				miConnection
				);
		bFlag = true;
	}

	if( bFlag ) {
		client->Reset();
		client->socket = socket;
		socket->data = client;

		LogManager::GetLogManager()->Log(
				LOG_MSG,
				"AsyncIOServer::OnAccept( "
				"client : %p, "
				"socket : %p, "
				"ip : %s, "
				"port : %d, "
				"miConnection : %u "
				")",
				client,
				socket,
				socket->ip.c_str(),
				socket->port,
				miConnection
				);

		bFlag = false;
		if( mpAsyncIOServerCallback ) {
			bFlag = mpAsyncIOServerCallback->OnAccept(client);
		}

		mServerMutex.lock();
		// 增加在线数目
		miConnection++;
		mServerMutex.unlock();
	}

	return bFlag;
}

void AsyncIOServer::OnRecvEvent(Socket* socket) {
	Client* client = (Client *)(socket->data);
	if( client != NULL ) {
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"AsyncIOServer::OnRecvEvent( "
				"[Start], "
				"client : %p, "
				"socket : %p "
				")",
				client,
				socket
				);

		// 尝试读取数据
		char buf[READ_BUFFER_SIZE];
		int len = sizeof(buf) - 1;
		SocketStatus status = SocketStatusFail;
		bool disconnect = false;

		client->clientMutex.lock();
		while (true) {
			status = mTcpServer.Read(socket, (char *)buf, len);
			if( status == SocketStatusSuccess ) {
				// 读取数据成功, 缓存到客户端
				buf[len] = '\0';
				LogManager::GetLogManager()->Log(
						LOG_MSG,
						"AsyncIOServer::OnRecvEvent( "
						"[Read OK], "
						"client : %p, "
						"socket : %p, "
						"ip : %s, "
						"port : %u, "
						"len : %d, "
						"buf :\n%s\n"
						")",
						client,
						socket,
						socket->ip.c_str(),
						socket->port,
						len,
						buf
						);

				if( client->Write(buf, len) ) {
					LogManager::GetLogManager()->Log(
							LOG_MSG,
							"AsyncIOServer::OnRecvEvent( "
							"[Write buffer OK], "
							"client : %p, "
							"socket : %p, "
							"ip : %s, "
							"port : %u "
							")",
							client,
							socket,
							socket->ip.c_str(),
							socket->port
							);
					// 放到处理队列
					PushRecvHandle(client);

				} else {
					// 没有足够的缓存空间
					LogManager::GetLogManager()->Log(
							LOG_ERR_USER,
							"AsyncIOServer::OnRecvEvent( "
							"[Write error, buffer is not enough], "
							"client : %p, "
							"socket : %p, "
							"ip : %s, "
							"port : %u "
							")",
							client,
							socket,
							socket->ip.c_str(),
							socket->port
							);
					disconnect = true;
					break;
				}
			} else if( status == SocketStatusTimeout ) {
				// 没有数据可读超时返回, 不处理
				LogManager::GetLogManager()->Log(
						LOG_STAT,
						"AsyncIOServer::OnRecvEvent( "
						"[Nothing to read], "
						"client : %p, "
						"socket : %p "
						")",
						client,
						socket
						);
				break;

			} else {
				// 读取数据出错, 断开
				LogManager::GetLogManager()->Log(
						LOG_MSG,
						"AsyncIOServer::OnRecvEvent( "
						"[Read error], "
						"client : %p, "
						"socket : %p, "
						"ip : %s, "
						"port : %u "
						")",
						client,
						socket,
						socket->ip.c_str(),
						socket->port
						);
				disconnect = true;
				break;
			}
		}

		// 如果读出错, 断开连接
		client->disconnected = disconnect;
//		if( disconnect ) {
//			// 断开连接
//			Disconnect(client);
//		}

		bool bFlag = false;
		bFlag = ClientCloseIfNeed(client);
		client->clientMutex.unlock();

		// 销毁客户端
		if( bFlag ) {
			DestroyClient(client);
		}

		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"AsyncIOServer::OnRecvEvent( "
				"[Exit], "
				"client : %p, "
				"socket : %p "
				")",
				client,
				socket
				);
	}
}

void AsyncIOServer::OnDisconnect(Socket* socket) {
	Client* client = (Client *)(socket->data);
	if( client != NULL ) {
		client->clientMutex.lock();
		client->disconnected = true;

		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"AsyncIOServer::OnDisconnect( "
				"client : %p, "
				"socket : %p, "
				"ip : %s, "
				"port : %u, "
				"recvHandleCount : %d "
				") \n",
				client,
				socket,
				socket->ip.c_str(),
				socket->port,
				client->recvHandleCount
				);

		bool bFlag = ClientCloseIfNeed(client);

		client->clientMutex.unlock();

		// 销毁客户端
		if( bFlag ) {
			DestroyClient(client);
		}
	}
}

void AsyncIOServer::RecvHandleThread() {
	LogManager::GetLogManager()->Log(LOG_STAT, "AsyncIOServer::RecvHandleThread( [Start] )");

	Client* client = NULL;
	bool bFlag = false;

	while( mRunning ) {
		if ( (client = mClientHandleList.PopFront()) ) {
			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"AsyncIOServer::RecvHandleThread( "
					"[Parse, Start], "
					"client : %p "
					")",
					client
					);

			client->clientMutex.lock();
			// 开始处理
			client->Parse();

			// 减少处理数
			client->recvHandleCount--;

			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"AsyncIOServer::RecvHandleThread( "
					"[Parse, Exit], "
					"client : %p "
					")",
					client
					);

			// 如果已经断开连接关闭socket
			bFlag = ClientCloseIfNeed(client);
			client->clientMutex.unlock();

			// 销毁客户端
			if( bFlag ) {
				DestroyClient(client);
			}

		} else {
			usleep(100 * 1000);
		}
	}

	LogManager::GetLogManager()->Log(LOG_STAT, "AsyncIOServer::RecvHandleThread( [Exit] )");
}

void AsyncIOServer::PushRecvHandle(Client* client) {
	client->clientMutex.lock();

	// 增加计数器
	client->recvHandleCount++;

	// 放到处理队列
	mClientHandleList.PushBack(client);

	client->clientMutex.unlock();
}

bool AsyncIOServer::ClientCloseIfNeed(Client* client) {
	bool bFlag = false;
	if( client->recvHandleCount == 0 && client->disconnected ) {
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"AsyncIOServer::ClientCloseIfNeed( "
				"client : %p, "
				"ip : %s, "
				"port : %u "
				")",
				client,
				client->socket->ip.c_str(),
				client->socket->port
				);

		if( mpAsyncIOServerCallback ) {
			mpAsyncIOServerCallback->OnDisconnect(client);
		}

		// 关闭Socket
		mTcpServer.Close(client->socket);

		bFlag = true;
	}

	return bFlag;
}

void AsyncIOServer::DestroyClient(Client* client) {
	mServerMutex.lock();
	// 减少在线数目
	miConnection--;
	mServerMutex.unlock();

	// 归还内存
	if( mClientIdleList.Size() <= (size_t)miMaxConnection ) {
		// 空闲的缓存小于设定值
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"AsyncIOServer::DestroyClient( "
				"[Return client to idle list], "
				"client : %p "
				")",
				client
				);

		mClientIdleList.PushBack(client);

	} else {
		LogManager::GetLogManager()->Log(
				LOG_WARNING,
				"AsyncIOServer::DestroyClient( "
				"[Delete extra client], "
				"client : %p "
				")",
				client
				);

		// 释放内存
		Client::Destroy(client);

	}
}
