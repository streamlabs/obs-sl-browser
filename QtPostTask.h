#include <future>
#include <vector>
#include <memory>
#include <QThread>

// Encapsulates context information for asynchronous calls.
struct AsyncCallContext
{
	std::string file;
	int line;

	AsyncCallContext(const std::string &file, int line) : file(file), line(line) {}
};

// Manages a stack of asynchronous call contexts.
class AsyncCallContextStack : public std::vector<std::unique_ptr<AsyncCallContext>>
{
public:
	~AsyncCallContextStack()
	{
		clear(); // unique_ptr handles memory deallocation automatically.
	}
};

// Global stack for managing asynchronous call contexts.
static AsyncCallContextStack asyncCallContextStack;

// Retrieves the current stack of asynchronous call contexts.
const AsyncCallContextStack *GetAsyncCallContextStack()
{
	return &asyncCallContextStack;
}

// Pushes a new context onto the stack.
static void PushAsyncCallContext(const std::string &file, int line)
{
	asyncCallContextStack.emplace_back(std::make_unique<AsyncCallContext>(file, line));
}

// Pops the last context from the stack.
static void PopAsyncCallContext()
{
	asyncCallContextStack.pop_back();
}

// Implementation for posting a task to the Qt event loop.
std::future<void> PostQtTaskImpl(std::function<void()> task, const std::string &file, int line)
{
	auto promise = std::make_shared<std::promise<void>>();
	auto executor = [task, promise, file, line]() {
		PushAsyncCallContext(file, line);
		task();
		PopAsyncCallContext();
		promise->set_value();
	};
	QMetaObject::invokeMethod(qApp, executor, Qt::QueuedConnection);
	return promise->get_future();
}

// Executes a task synchronously or posts it asynchronously depending on the thread context.
std::future<void> ExecOrPostQtTask(std::function<void()> task, const std::string &file, int line)
{
	if (QThread::currentThread() == qApp->thread())
	{
		task();
		std::promise<void> promise;
		promise.set_value();
		return promise.get_future();
	}
	else
	{
		return PostQtTaskImpl(task, file, line);
	}
}

// Functor to manage asynchronous Qt calls with contextual information.
class QtAsyncCall
{
private:
	using CallImpl = std::future<void> (*)(std::function<void()>, const std::string &, int);

	std::string file;
	int line;
	CallImpl impl;

public:
	QtAsyncCall(const char *file, int line, CallImpl impl) : file(file), line(line), impl(impl) {}

	std::future<void> operator()(std::function<void()> task) const { return impl(task, file, line); }
};

// Macro to simplify posting tasks with context.
#define QtPostTask QtAsyncCall(__FILE__, __LINE__, &PostQtTaskImpl)
