/*
 * TcpServer.cpp
 *
 *  Created on: 2016年9月12日
 *      Author: Max.Chiu
 */

#include "TcpServer.h"
#include "Socket.h"

#include <ev.h>

/***************************** libev回调函数 **************************************/
void accept_handler(struct ev_loop *loop, ev_io *w, int revents);
void recv_handler(struct ev_loop *loop, ev_io *w, int revents);

void accept_handler(struct ev_loop *loop, ev_io *w, int revents) {
	TcpServer *pTcpServer = (TcpServer *)ev_userdata(loop);
	pTcpServer->IOHandleAccept(w, revents);
}

void recv_handler(struct ev_loop *loop, ev_io *w, int revents) {
	TcpServer *pTcpServer = (TcpServer *)ev_userdata(loop);
	pTcpServer->IOHandleRecv(w, revents);
}
/***************************** libev回调函数 **************************************/

class IORunnable : public KRunnable {
public:
	IORunnable(TcpServer *container) {
		mContainer = container;
	}
	virtual ~IORunnable() {
		mContainer = NULL;
	}
protected:
	void onRun() {
		mContainer->IOHandleThread();
	}
private:
	TcpServer *mContainer;
};

TcpServer::TcpServer()
:mServerMutex(KMutex::MutexType_Recursive)
{
	// TODO Auto-generated constructor stub
	mpTcpServerCallback = NULL;

	mRunning = false;

	mpIORunnable = new IORunnable(this);

	mLoop = NULL;

	mpSocket = Socket::Create();
	miMaxConnection = 1;

	mpAcceptWatcher = (ev_io *)malloc(sizeof(ev_io));
}

TcpServer::~TcpServer() {
	// TODO Auto-generated destructor stub
	Stop();

	if( mpIORunnable ) {
		delete mpIORunnable;
		mpIORunnable = NULL;
	}

	if( mpSocket ) {
		Socket::Destroy(mpSocket);
		mpSocket = NULL;
	}

	if( mpAcceptWatcher ) {
		free(mpAcceptWatcher);
		mpAcceptWatcher = NULL;
	}
}

void TcpServer::SetTcpServerCallback(TcpServerCallback* callback) {
	mpTcpServerCallback = callback;
}

bool TcpServer::Start(int port, int maxConnection, const char *ip) {
	bool bFlag = true;

	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"TcpServer::Start( "
			"[Start], "
			"port : %u, "
			"maxConnection : %d, "
			"ip : %s "
			")",
			port,
			maxConnection,
			ip
			);

	mServerMutex.lock();
	if( mRunning ) {
		Stop();
	}
	mRunning = true;
	miMaxConnection = maxConnection;

	// 创建socket
	int fd = INVALID_SOCKET;
	struct sockaddr_in ac_addr;
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		LogManager::GetLogManager()->Log(
				LOG_ERR_SYS,
				"TcpServer::Start( "
				"[Create socket error], "
				"port : %d, "
				"maxConnection : %d, "
				"ip : %s "
				")",
				port,
				maxConnection,
				ip
				);
		printf("# TcpServer Create socket error, "
				"port : %d, "
				"maxConnection : %d, "
				"ip : %s "
				"\n",
				port,
				maxConnection,
				ip
				);
		bFlag = false;
	}

	if( bFlag ) {
		mpSocket->SetAddress(fd, ip, port);

		// 设置快速重用
		int iFlag = 1;
		setsockopt(mpSocket->fd, SOL_SOCKET, SO_REUSEADDR, &iFlag, sizeof(iFlag));
		// 设置非阻塞
		int flags = fcntl(mpSocket->fd, F_GETFL, 0);
		flags = flags | O_NONBLOCK;
		fcntl(mpSocket->fd, F_SETFL, flags);
	}

	if( bFlag ) {
		// 绑定端口和IP地址
		bzero(&ac_addr, sizeof(ac_addr));
		ac_addr.sin_family = PF_INET;
		ac_addr.sin_port = htons(port);
		ac_addr.sin_addr.s_addr = INADDR_ANY;

		if ( bind(mpSocket->fd, (struct sockaddr *) &ac_addr, sizeof(struct sockaddr)) == -1 ) {
			LogManager::GetLogManager()->Log(
					LOG_ERR_SYS,
					"TcpServer::Start( "
					"[Bind socket error], "
					"port : %d, "
					"maxConnection : %d, "
					"ip : %s "
					")",
					port,
					maxConnection,
					ip
					);
			printf("# Bind socket error, "
					"port : %d, "
					"maxConnection : %d, "
					"ip : %s "
					"\n",
					port,
					maxConnection,
					ip
					);
			bFlag = false;
		}
	}

	if( bFlag ) {
		if ( listen(mpSocket->fd, 1024) == -1 ) {
			LogManager::GetLogManager()->Log(
					LOG_ERR_SYS,
					"TcpServer::Start( "
					"[Listen socket error], "
					"port : %d, "
					"maxConnection : %d, "
					"ip : %s "
					")",
					port,
					maxConnection,
					ip
					);
			printf("# Listen socket error, "
					"port : %d, "
					"maxConnection : %d, "
					"ip : %s "
					"\n",
					port,
					maxConnection,
					ip
					);
			bFlag = false;
		}
	}

	if( bFlag ) {
		/* create watchers */
		for(int i = 0 ; i < maxConnection; i++) {
			ev_io *w = (ev_io *)malloc(sizeof(ev_io));
			if( w != NULL ) {
				mWatcherList.PushBack(w);
			}
		}
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"TcpServer::Start( "
				"[Create watchers OK], "
				"port : %d, "
				"maxConnection : %d, "
				"ip : %s, "
				"mWatcherList : %d "
				")",
				port,
				maxConnection,
				ip,
				mWatcherList.Size()
				);
	}

	if( bFlag ) {
		mLoop = ev_loop_new(EVFLAG_AUTO);//EV_DEFAULT;
	}

	if( bFlag ) {
		// 启动IO监听线程
		if( 0 == mIOThread.start(mpIORunnable) ) {
			LogManager::GetLogManager()->Log(
					LOG_ERR_SYS,
					"TcpServer::Start( [Create IO thread Fail], "
					"port : %d, "
					"maxConnection : %d, "
					"ip : %s "
					")",
					port,
					maxConnection,
					ip
					);
			bFlag = false;
		}
	}

	if( bFlag ) {
		LogManager::GetLogManager()->Log(
				LOG_MSG,
				"TcpServer::Start( "
				"[OK], "
				"port : %d, "
				"maxConnection : %d, "
				"ip : %s "
				")",
				port,
				maxConnection,
				ip
				);
		printf("# TcpServer start OK, "
				"port : %d, "
				"maxConnection : %d, "
				"ip : %s "
				"\n",
				port,
				maxConnection,
				ip
				);
	} else {
		LogManager::GetLogManager()->Log(
				LOG_ERR_SYS,
				"TcpServer::Start( "
				"[Fail], "
				"port : %d, "
				"maxConnection : %d, "
				"ip : %s "
				")",
				port,
				maxConnection,
				ip
				);
		printf("# TcpServer start fail, "
				"port : %d, "
				"maxConnection : %d, "
				"ip : %s "
				"\n",
				port,
				maxConnection,
				ip
				);
		Stop();
	}

	mServerMutex.unlock();

	return bFlag;
}

void TcpServer::Stop() {
	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"TcpServer::Stop( "
			"[Start], "
			"port : %u, "
			"maxConnection : %d, "
			"ip : %s "
			")",
			mpSocket->port,
			miMaxConnection,
			mpSocket->ip.c_str()
			);

	mServerMutex.lock();

	if( mRunning ) {
		mRunning = false;

//		// 停止IO线程
//		if( mLoop ) {
//			ev_stop(mLoop);
//		}

		// 停止监听socket事件
		mWatcherMutex.lock();
		ev_io_stop(mLoop, mpAcceptWatcher);
		mWatcherMutex.unlock();

		// 关掉监听socket
		mpSocket->Disconnect();

		// 等待IO线程停止
		mIOThread.stop();

		// 关闭监听socket
		mpSocket->Close();

		// 清除监听器队列
		ev_io* w = NULL;
		while( NULL != ( w = mWatcherList.PopFront() ) ) {
			delete w;
		}

		if( mLoop ) {
			ev_loop_destroy(mLoop);
		}
	}

	mServerMutex.unlock();

	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"TcpServer::Stop( "
			"[OK], "
			"port : %u, "
			"maxConnection : %d, "
			"ip : %s "
			")",
			mpSocket->port,
			miMaxConnection,
			mpSocket->ip.c_str()
			);

}

bool TcpServer::IsRunning() {
	return mRunning;
}

SocketStatus TcpServer::Read(Socket* socket, const char *data, int &len) {
	SocketStatus status = socket->Read(data, len);

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::Read( "
			"fd : %d, "
			"socket : %p, "
			"status : %d "
			")",
			socket->fd,
			socket,
			status
			);

	if( status == SocketStatusFail ) {
		// 读取数据失败, 停止监听epoll
		StopEvIO(socket->w);
	}

	return status;
}

bool TcpServer::Send(Socket* socket, const char *data, int &len) {
	return socket->Send(data, len);
}

void TcpServer::Disconnect(Socket* socket) {
	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::Disconnect( "
			"fd : %d, "
			"socket : %p "
			")",
			socket->fd,
			socket
			);

	// 断开连接
	socket->Disconnect();
}

void TcpServer::Close(Socket* socket) {
	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::Close( "
			"fd : %d, "
			"socket : %p "
			")",
			socket->fd,
			socket
			);

	// 关掉连接socket
	socket->Close();

	// 释放内存
	Socket::Destroy(socket);
}

void TcpServer::IOHandleThread() {
	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"TcpServer::IOHandleThread( [Start] )"
			);

	// 把mServer放到事件监听队列
	ev_io_init(mpAcceptWatcher, accept_handler, mpSocket->fd, EV_READ);
	mpAcceptWatcher->data = mpSocket;
	ev_io_start(mLoop, mpAcceptWatcher);

	// 增加回调参数
	ev_set_userdata(mLoop, this);

	// 执行epoll_wait
	ev_run(mLoop, 0);

	LogManager::GetLogManager()->Log(
			LOG_MSG,
			"TcpServer::IOHandleThread( "
			"[Exit]"
			")"
			);
}

void TcpServer::IOHandleAccept(ev_io *w, int revents) {
	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::IOHandleAccept( "
			"[Start] "
//			"fd : %d "
			")"
//			w->fd
			);

	int clientfd = 0;
	struct sockaddr_in addr;
	socklen_t iAddrLen = sizeof(struct sockaddr);
	while ( (clientfd = accept(w->fd, (struct sockaddr *)&addr, &iAddrLen)) < 0 ) {
		if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"TcpServer::IOHandleAccept( "
					"[errno == EAGAIN ||errno == EWOULDBLOCK] "
//					"fd : %d "
					")"
//					w->fd
					);
			continue;
		} else {
			LogManager::GetLogManager()->Log(
					LOG_WARNING,
					"TcpServer::AcceptCallback( "
					"[Accept error] "
//					"fd : %d "
					")"
//					w->fd
					);
			break;
		}
	}

	if( clientfd != INVALID_SOCKET ) {
		// 创建连接结构体
		char* ip = inet_ntoa(addr.sin_addr);
		Socket* socket = Socket::Create();
		socket->SetAddress(clientfd, ip, addr.sin_port);

		bool bAccept = false;
		if( mpTcpServerCallback && (bAccept = mpTcpServerCallback->OnAccept(socket)) ) {
			int iFlag = 1;
			// 设置非阻塞
			int flags = fcntl(clientfd, F_GETFL, 0);
			flags = flags | O_NONBLOCK;
			fcntl(clientfd, F_SETFL, flags);

		    // 设置ACK马上回复
			setsockopt(clientfd, IPPROTO_TCP, TCP_NODELAY, &iFlag, sizeof(iFlag));
			// CloseSocketIfNeed（一般不会立即关闭而经历TIME_WAIT的过程）后想继续重用该socket
			setsockopt(clientfd, SOL_SOCKET, SO_REUSEADDR, &iFlag, sizeof(iFlag));
			// 如果在发送数据的时，希望不经历由系统缓冲区到socket缓冲区的拷贝而影响
			int nZero = 0;
			setsockopt(clientfd, SOL_SOCKET, SO_SNDBUF, &nZero, sizeof(nZero));

			/*deal with the tcp keepalive
			  iKeepAlive = 1 (check keepalive)
			  iKeepIdle = 600 (active keepalive after socket has idled for 10 minutes)
			  KeepInt = 60 (send keepalive every 1 minute after keepalive was actived)
			  iKeepCount = 3 (send keepalive 3 times before disconnect from peer)
			 */
			int iKeepAlive = 1, iKeepIdle = 60, KeepInt = 20, iKeepCount = 3;
			setsockopt(clientfd, SOL_SOCKET, SO_KEEPALIVE, (void*)&iKeepAlive, sizeof(iKeepAlive));
			setsockopt(clientfd, IPPROTO_TCP, TCP_KEEPIDLE, (void*)&iKeepIdle, sizeof(iKeepIdle));
			setsockopt(clientfd, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&KeepInt, sizeof(KeepInt));
			setsockopt(clientfd, IPPROTO_TCP, TCP_KEEPCNT, (void *)&iKeepCount, sizeof(iKeepCount));

			ev_io *watcher = NULL;
			if( (watcher = mWatcherList.PopFront()) != NULL ) {
				// 接受连接
				LogManager::GetLogManager()->Log(
						LOG_STAT,
						"TcpServer::IOHandleAccept( "
						"[Accept client], "
						"fd : %d, "
						"socket : %p, "
						"watcher : %p "
						")",
						clientfd,
						socket,
						watcher
						);

			} else {
				watcher = (ev_io *)malloc(sizeof(ev_io));

				LogManager::GetLogManager()->Log(
						LOG_WARNING,
						"TcpServer::IOHandleAccept( "
						"[Not enough watcher, new more], "
						"fd : %d, "
						"socket : %p, "
						"watcher : %p "
						")",
						clientfd,
						socket,
						watcher
						);
			}

			watcher->data = socket;
			socket->w = watcher;
			ev_io_init(watcher, recv_handler, clientfd, EV_READ);

			mWatcherMutex.lock();
			ev_io_start(mLoop, watcher);
			mWatcherMutex.unlock();

		} else {
			LogManager::GetLogManager()->Log(
					LOG_WARNING,
					"TcpServer::IOHandleAccept( "
					"[Not allow accept client], "
					"fd : %d, "
					"socket : %p "
					")",
					clientfd,
					socket
					);
			// 拒绝连接
			bAccept = false;
		}

		if( !bAccept ) {
			// 断开连接
			Disconnect(socket);

			// 回调
			IOHandleOnDisconnect(socket);

//			// 关闭连接
//			Close(socket);
		}
	}

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::IOHandleAccept( "
			"[Exit] "
//			"fd : %d "
			")"
//			clientfd
			);
}

void TcpServer::IOHandleRecv(ev_io *w, int revents) {
	Socket* socket = (Socket *)w->data;

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::IOHandleRecv( "
			"[Start], "
			"fd : %d, "
			"socket : %p, "
			"revents : %d "
			")",
			w->fd,
			socket,
			revents
			);

	if( revents & EV_ERROR ) {
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"TcpServer::IOHandleRecv( "
				"[revents & EV_ERROR], "
				"fd : %d, "
				"socket : %p "
				")",
				w->fd,
				socket
				);
		// 停止监听epoll
		StopEvIO(w);

		// 回调断开连接
		IOHandleOnDisconnect(socket);

	} else {
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"TcpServer::IOHandleRecv( "
				"[OnRecvEvent], "
				"fd : %d, "
				"socket : %p "
				")",
				w->fd,
				socket
				);
		if( mpTcpServerCallback != NULL ) {
			mpTcpServerCallback->OnRecvEvent(socket);
		}
	}

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::IOHandleRecv( "
			"[Exit], "
			"fd : %d, "
			"socket : %p "
			")",
			w->fd,
			socket
			);
}

void TcpServer::IOHandleOnDisconnect(Socket* socket) {
	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"TcpServer::IOHandleOnDisconnect( "
			"socket : %p "
			")",
			socket
			);

	// 回调断开连接
	if( mpTcpServerCallback != NULL ) {
		mpTcpServerCallback->OnDisconnect(socket);
	}
}

void TcpServer::StopEvIO(ev_io *w) {
	if( w != NULL ) {
		int fd = w->fd;
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"TcpServer::StopEvIO( "
				"fd : %d "
				")",
				fd
				);

		// 停止监听epoll
		mWatcherMutex.lock();
		ev_io_stop(mLoop, w);
		mWatcherMutex.unlock();

		if( mWatcherList.Size() <= (size_t)miMaxConnection ) {
			// 空闲的缓存小于设定值
			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"TcpServer::StopEvIO( "
					"[Return ev_io to idle list], "
					"fd : %d "
					")",
					fd
					);

			mWatcherList.PushBack(w);
		} else {
			// 释放内存
			LogManager::GetLogManager()->Log(
					LOG_WARNING,
					"TcpServer::StopEvIO( "
					"[Delete extra ev_io], "
					"fd : %d "
					")",
					fd
					);

			free(w);
		}
	}
}
