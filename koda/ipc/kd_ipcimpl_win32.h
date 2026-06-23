/** **********************************************************************
Created by: Arlin (arlins.dps@gmail.com).

This file implements IPCConnectionImpl on Windows
It is implemented based on asynchronous pipes using byte streams.
********************************************************************** **/

#pragma once
#include "koda/ipc/kd_ipcdefs.h"

// =====================
// Implementation - Win32
// =====================
#if defined(KD_OS_WIN)
#include <windows.h>
#include <sddl.h>

namespace ipc_detail {
// Close object handle
inline void closeObjectHandle(HANDLE& handle) {
	if (handle != NULL) {
		CloseHandle(handle);
		handle = NULL;
	}
}

// Close file handle
inline void closeFileHandle(HANDLE& handle) {
	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
	}
}

inline void zeroOverlapped(LPOVERLAPPED lpOverlapped) {
	if (lpOverlapped) {
		HANDLE hSavedEvent = lpOverlapped->hEvent;
		ZeroMemory(lpOverlapped, sizeof(*lpOverlapped));
		lpOverlapped->hEvent = hSavedEvent;
	}
}
}

//
__NAMESPACE_KD_BEGIN

// ======================
// IPCConnectionImplWin
// ======================
class IPCConnectionImplWin : public IPCConnectionImpl {
public:
	IPCConnectionImplWin(IPCDelegate* delegate, const std::string& channel, bool isServer,
		unsigned int readBufferSize, unsigned int writeBufferSize)
		: IPCConnectionImpl(delegate, channel, isServer, readBufferSize, writeBufferSize) {
		m_pipePath = std::string("\\\\.\\pipe\\FA8C051288F64B16A0714FD3D4F4B2E0_ipc_async_pipe_" + channel);
		m_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		m_connectOverlap.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		m_readOverlap.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		m_writeOverlap.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

		ZeroMemory(&m_securityAttributes, sizeof(m_securityAttributes));
		ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;WD)",
			SDDL_REVISION_1, &(m_securityAttributes.lpSecurityDescriptor), NULL);
	}

	~IPCConnectionImplWin() {
		_cleanUpPipe();
		if (m_securityAttributes.lpSecurityDescriptor) {
			LocalFree(m_securityAttributes.lpSecurityDescriptor);
		}

		ipc_detail::closeObjectHandle(m_hStopEvent);
		ipc_detail::closeObjectHandle(m_connectOverlap.hEvent);
		ipc_detail::closeObjectHandle(m_readOverlap.hEvent);
		ipc_detail::closeObjectHandle(m_writeOverlap.hEvent);
	}

public:
	bool open(const std::function<void()>& aboutToConnect,
		const std::function<void(bool)>& didConnected) override {
		ResetEvent(m_hStopEvent);
		ResetEvent(m_connectOverlap.hEvent);
		ResetEvent(m_readOverlap.hEvent);
		ResetEvent(m_writeOverlap.hEvent);

		if (m_isServer) {
			return _openServer(aboutToConnect, didConnected);
		} else {
			return _openClient(aboutToConnect, didConnected);
		}
	};

	void close() override {
		_cleanUpPipe();
	};

	bool ready() override {
		return m_hPipe != INVALID_HANDLE_VALUE;
	}

	void interrupt() override {
		kd_ipc_debug("Interrupt the connection");
		// Wake up all waiting Events
		if (m_hStopEvent != NULL) {
			SetEvent(m_hStopEvent);
		}

		// Cancel all ongoing I/O operations on the pipe handle 
		// CancelIoEx will Cancel ReadFile, ConnectNamedPipe...
		HANDLE hPipe = INVALID_HANDLE_VALUE;
		{
			std::lock_guard<IPCMutex> lock(m_connMtx);
			hPipe = m_hPipe;
		}
		if (hPipe != INVALID_HANDLE_VALUE) {
			CancelIoEx(hPipe, NULL);
		}
	};

	bool read(void* data, uint32_t size) override {
		if (size == 0 || data == nullptr) {
			return true;
		}

		HANDLE hPipe = INVALID_HANDLE_VALUE;
		{
			std::lock_guard<IPCMutex> lock(m_connMtx);
			hPipe = m_hPipe;
		}
		if (hPipe == INVALID_HANDLE_VALUE) {
			kd_ipc_debug("Read failed: Handle is invalid");
			return false; // Failed
		}

		DWORD totalRead = 0;
		BYTE* pBuffer = static_cast<BYTE*>(data);

		// Start reading loop
		while (totalRead < size && !m_delegate->isQuit()) {
			ipc_detail::zeroOverlapped(&m_readOverlap);
			ResetEvent(m_readOverlap.hEvent);
			DWORD bytesToRead = size - totalRead;

			BOOL ok = ReadFile(hPipe, pBuffer + totalRead, bytesToRead, NULL, &m_readOverlap);
			if (!ok && GetLastError() != ERROR_IO_PENDING) {
				kd_ipc_debug("Read failed: ReadFile failed, error = %d", GetLastError());
				return false; // Failed
			}

			HANDLE waitHandles[2] = { m_hStopEvent, m_readOverlap.hEvent };
			DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

			if (waitRes != WAIT_OBJECT_0 + 1 || m_delegate->isQuit()) {
				CancelIoEx(hPipe, &m_readOverlap);
				if (m_delegate->isQuit()) {
					kd_ipc_debug("Read interrupted: quit signaled.");
				} else {
					kd_ipc_debug("Read failed: wait result = %d, error = %d", waitRes, GetLastError());
				}
				return false; // Failed
			}

			// WAIT_OBJECT_0 + 1: Data has been read
			DWORD transferred = 0;
			BOOL overlappedResult = GetOverlappedResult(hPipe, &m_readOverlap, &transferred, FALSE);
			if (overlappedResult) {
				if (transferred > 0) {
					totalRead += transferred; // Success
					continue; // Continue to read
				} else {
					// Check if the pipe is still valid.
					if (!GetNamedPipeHandleState(hPipe, NULL, NULL, NULL, NULL, NULL, 0)) {
						kd_ipc_debug("Read failed: Read 0 bytes and pipe was broken. Error = %d", GetLastError());
						return false; // Failed
					}

					// The pipe is valid but 0 bytes have been read
					kd_ipc_debug("Read failed: Read 0 bytes with valid pipe");
					return false; // Failed
				}
			} else {
				DWORD err = GetLastError();
				if (err == ERROR_MORE_DATA) {
					totalRead += transferred; // Success
					continue; // Continue to read
				} else {
					kd_ipc_debug("Read failed: GetOverlappedResult failed, error = %d", err);
					return false; // Failed
				}
			} // overlappedResult

		} // while


		// Finally
		bool success = (totalRead == size);
		if (!success) {
			kd_ipc_debug("Read unfinished: partial data read (%u/%u). Quit signaled: %d",
				totalRead, size, m_delegate->isQuit());
		}

		return success;
	};

	bool write(const void* data, uint32_t size) override {
		if (size == 0 || data == nullptr) {
			return true;
		}

		HANDLE hPipe = INVALID_HANDLE_VALUE;
		{
			std::lock_guard<IPCMutex> lock(m_connMtx);
			hPipe = m_hPipe;
		}
		if (hPipe == INVALID_HANDLE_VALUE) {
			kd_ipc_debug("Write failed: Handle is invalid");
			return false; // Failed
		}

		DWORD totalWritten = 0;
		const BYTE* pBuffer = static_cast<const BYTE*>(data);

		// Start writing loop
		while (totalWritten < size && !m_delegate->isQuit()) {
			ipc_detail::zeroOverlapped(&m_writeOverlap);
			ResetEvent(m_writeOverlap.hEvent);
			DWORD bytesToWrite = size - totalWritten;

			// Write the data asynchronously
			// If WriteFile returns TRUE, it only means that the IO request has been submitted, 
			// not that the data has been sent to the peer. Even if the data is sent successfully and 
			// returns True, the event object will still be signaled. Therefore, we always use whether 
			// the event object is signaled as the criterion.
			BOOL ok = WriteFile(hPipe, pBuffer + totalWritten, bytesToWrite, NULL, &m_writeOverlap);
			if (!ok && GetLastError() != ERROR_IO_PENDING) {
				kd_ipc_debug("Write failed: WriteFile failed, error = %d", GetLastError());
				return false; // Failed
			}

			// Wait for writing result
			HANDLE waitHandles[2] = { m_hStopEvent, m_writeOverlap.hEvent };
			DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

			if (waitRes != WAIT_OBJECT_0 + 1 || m_delegate->isQuit()) {
				CancelIoEx(hPipe, &m_writeOverlap);
				if (m_delegate->isQuit()) {
					kd_ipc_debug("Write interrupted: quit signaled.");
				} else {
					kd_ipc_debug("Write failed: wait result = %d, error = %d", waitRes, GetLastError());
				}
				return false; // Failed
			}

			// WAIT_OBJECT_0 + 1: Data has been written
			DWORD transferred = 0;
			BOOL overlappedResult = GetOverlappedResult(hPipe, &m_writeOverlap, &transferred, FALSE);
			if (overlappedResult) {
				if (transferred > 0) {
					totalWritten += transferred; // Success
					continue; // Continue to write
				} else {
					// GetOverlappedResult succeeded but wrote 0 bytes when the requested byte 
					// was not 0. This could mean that there may be some anomalies on the other end.

					// Check if the pipe is still valid.
					if (!GetNamedPipeHandleState(hPipe, NULL, NULL, NULL, NULL, NULL, 0)) {
						kd_ipc_debug("Write failed: Write 0 bytes and pipe was broken. Error = %d", GetLastError());
						return false; // Failed
					}

					// The pipe is valid but 0 bytes have been written, this could be an extremely 
					// rare edge case in kernel scheduling. For safety, we return failure, allowing the 
					// user to initiate a reconnection if necessary.
					kd_ipc_debug("Write failed: Write 0 bytes with valid pipe");
					return false; // Failed
				} // transferred
			} else {
				kd_ipc_debug("Write failed: GetOverlappedResult failed, error = %d", GetLastError());
				return false; // Failed
			} // overlappedResult

		} // while

		// Finally
		bool success = (totalWritten == size);
		if (!success) {
			kd_ipc_debug("Write unfinished: partial data written (%u/%u). Quit signaled: %d",
				totalWritten, size, m_delegate->isQuit());
		}

		return success;
	};

private:
	// Open server and connect to client
	bool _openServer(const std::function<void()>& aboutToConnect,
		const std::function<void(bool)>& didConnected) {
		KD_ASSERT_M(m_hPipe == INVALID_HANDLE_VALUE, "Connection is not closed");

		// Create a new pipe instance
		HANDLE hNewPipe = CreateNamedPipeA(
			m_pipePath.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
			10, // PIPE_UNLIMITED_INSTANCES
			m_writeBufferSize,  // Write buffer size
			m_readBufferSize,   // Read buffer size
			0,
			&m_securityAttributes);

		if (hNewPipe == INVALID_HANDLE_VALUE) {
			kd_ipc_debug("Failed to create pipe, error = %d", GetLastError());
			return false; // Failed
		}
		if (m_delegate->isQuit()) {
			CloseHandle(hNewPipe);
			return false; // Failed
		}

		// Waiting for connection
		std::string errorString;
		bool connected = false;
		if (aboutToConnect) {
			aboutToConnect();
		}

		ipc_detail::zeroOverlapped(&m_connectOverlap);
		ResetEvent(m_connectOverlap.hEvent);
		BOOL ok = ConnectNamedPipe(hNewPipe, &m_connectOverlap);
		if (m_delegate->isQuit()) {
			_cleanUpPipe(hNewPipe);
			if (didConnected) {
				didConnected(false);
			}
			return false; // Failed
		}

		// Check if connected
		if (ok) { // Connected after calling ConnectNamedPipe
			connected = true; // Connected
		} else {
			DWORD errorCode = GetLastError();
			if (errorCode == ERROR_PIPE_CONNECTED) { // Already Connected
				connected = true; // Connected
			} else if (errorCode == ERROR_IO_PENDING) { // IO pending
				// When multiple events of WaitForMultipleObjects are triggered 
				// simultaneously, it will return the smaller index value. Therefore, 
				// we put the stop event first.
				HANDLE waitHandles[2] = { m_hStopEvent, m_connectOverlap.hEvent };
				DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

				if (waitRes != WAIT_OBJECT_0 + 1 || m_delegate->isQuit()) {
					_cleanUpPipe(hNewPipe);
					if (m_delegate->isQuit()) {
						kd_ipc_debug("Connect interrupted: quit signaled.");
					} else {
						kd_ipc_debug("Connect failed: wait result = %d, error = %d", waitRes, GetLastError());
					}
					if (didConnected) {
						didConnected(false);
					}
					return false; // Failed
				}

				// WAIT_OBJECT_0 + 1: Connection event signaled
				DWORD dummy;
				if (GetOverlappedResult(hNewPipe, &m_connectOverlap, &dummy, FALSE)) {
					connected = true; // Connected
				} else { // Failed
					kd_ipc_debug("Connect failed, GetOverlappedResult failed, error = %d", GetLastError());
				}
			} else { // Failed
				kd_ipc_debug("Connect failed, ConnectNamedPipe failed, error = %d", errorCode);
			}
		}

		// Finally
		if (connected) {
			// Assigning values ​​to pipe
			std::lock_guard<IPCMutex> lock(m_connMtx);
			m_hPipe = hNewPipe;
		} else {
			// Clean up if not connected
			_cleanUpPipe(hNewPipe);
		}
		if (didConnected) {
			didConnected(connected);
		}

		return connected;
	}

	bool _openClient(const std::function<void()>& aboutToConnect,
		const std::function<void(bool)>& didConnected) {
		KD_ASSERT_M(m_hPipe == INVALID_HANDLE_VALUE, "Connection is not closed");

		// Waiting for a pipe
		// WaitNamedPipe will return immediately if the pipe does not exist.
		// GetLastError() == ERROR_FILE_NOT_FOUND
		if (!WaitNamedPipeA(m_pipePath.c_str(), 1000)) {
			kd_ipc_debug("Failed to wait for server pipe, error = %d", GetLastError());
			return false;
		}
		if (m_delegate->isQuit()) {
			return false;
		}

		// Connecting server pipe
		std::string errorString;
		bool connected = false;
		if (aboutToConnect) {
			aboutToConnect();
		}

		HANDLE hNewPipe = CreateFileA(m_pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
			NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

		if (hNewPipe != INVALID_HANDLE_VALUE) { // Connected
			if (m_delegate->isQuit()) { // Quit
				CloseHandle(hNewPipe);
				errorString = "Quit by delegate";
			} else {
				DWORD dwMode = PIPE_READMODE_BYTE;
				SetNamedPipeHandleState(hNewPipe, &dwMode, NULL, NULL);
				connected = true; // Connected
			}
		} else { // Failed
			errorString = "CreateFile failed, error = " + std::to_string(GetLastError());
		}

		// Finally
		if (connected) {
			// Assigning values ​​to pipe
			std::lock_guard<IPCMutex> lock(m_connMtx);
			m_hPipe = hNewPipe;
		} else {
			// Clean up if not connected
			_cleanUpPipe(hNewPipe);
			kd_ipc_debug("Connection failed, error = %s", errorString.c_str());
		}
		if (didConnected) {
			didConnected(connected);
		}

		return connected;
	}

	void _cleanUpPipe(HANDLE hPipeToCleanup = INVALID_HANDLE_VALUE) {
		if (hPipeToCleanup == INVALID_HANDLE_VALUE || hPipeToCleanup == NULL) {
			std::lock_guard<IPCMutex> lock(m_connMtx);
			hPipeToCleanup = m_hPipe;
			m_hPipe = INVALID_HANDLE_VALUE;
		}
		if (hPipeToCleanup == INVALID_HANDLE_VALUE || hPipeToCleanup == NULL) {
			return;
		}

		// Cancel IO
		CancelIoEx(hPipeToCleanup, NULL);
		if (m_isServer) {
			DisconnectNamedPipe(hPipeToCleanup);
		}
		CloseHandle(hPipeToCleanup);
	}

private:
	std::string m_pipePath;
	OVERLAPPED m_connectOverlap = { 0 };
	OVERLAPPED m_readOverlap = { 0 };
	OVERLAPPED m_writeOverlap = { 0 };
	HANDLE m_hPipe{ INVALID_HANDLE_VALUE };
	HANDLE m_hStopEvent{ NULL };

	SECURITY_ATTRIBUTES m_securityAttributes;
};

__NAMESPACE_KD_END

#endif
