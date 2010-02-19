#include <signal.h>
#include <unistd.h>

#include <common/buffer.h>

#include <event/action.h>
#include <event/callback.h>
#include <event/event_system.h>

static void signal_reload(int);
static void signal_stop(int);

EventSystem::EventSystem(void)
: log_("/event/system"),
  queue_(),
  stop_(),
  interest_queue_(),
  timeout_queue_(),
  poll_()
{
	INFO(log_) << "Starting event system.";

	signal(SIGHUP, signal_reload);
	signal(SIGINT, signal_stop);
}

EventSystem::~EventSystem()
{
}

Action *
EventSystem::poll(const EventPoll::Type& type, int fd, EventCallback *cb)
{
	Action *a = poll_.poll(type, fd, cb);
	return (a);
}

Action *
EventSystem::register_interest(const EventInterest& interest, Callback *cb)
{
	Action *a = interest_queue_[interest].append(cb);
	return (a);
}

Action *
EventSystem::schedule(Callback *cb)
{
	Action *a = queue_.append(cb);
	return (a);
}

Action *
EventSystem::timeout(unsigned secs, Callback *cb)
{
	Action *a = timeout_queue_.append(secs, cb);
	return (a);
}

void
EventSystem::start(void)
{
	for (;;) {
		/*
		 * If we have been told to stop, fire all shutdown events.
		 */
		if (stop_ && !interest_queue_[EventInterestStop].empty()) {
			INFO(log_) << "Running stop handlers.";
			while (!interest_queue_[EventInterestStop].empty())
				interest_queue_[EventInterestStop].perform();
			INFO(log_) << "Stop handlers have been run.";
		}

		/*
		 * If there are time-triggered events whose time has come,
		 * run them.
		 */
		while (timeout_queue_.ready())
			timeout_queue_.perform();
		/*
		 * And then run a pending callback.
		 */
		queue_.perform();
		/*
		 * And if there are more pending callbacks, then do a quick
		 * poll and let them run.
		 *
		 * XXX
		 * There is actually no point in polling here.  If there are
		 * more queued Callbacks, any events fired by polling will be
		 * put after them.  We should take everything off the queue,
		 * drain that (with an eye on the clock) and then poll.  We can
		 * not just wait for the queue to be empty since it might never
		 * be.
		 */
		if (!queue_.empty()) {
			poll_.poll();
			continue;
		}
		/*
		 * But if there are no pending callbacks, and no timers or
		 * file descriptors being polled, stop.
		 */
		if (timeout_queue_.empty() && poll_.idle())
			break;
		/*
		 * If there are timers ticking down, then let the poll module
		 * block until a timer will be ready.
		 * XXX Maybe block 1 second less, just in case?  We'll never
		 * be a realtime system though, so any attempt at pretending
		 * to be one, beyond giving time events priority at the top of
		 * this loop, is probably a mistake.
		 */
		if (!timeout_queue_.empty()) {
			poll_.wait(timeout_queue_.interval());
			continue;
		}
		/*
		 * If, however, there's nothing sensitive to time, but someone
		 * has a file descriptor that can be blocked on, just block on it
		 * indefinitely.
		 */
		poll_.wait();
	}
}

void
EventSystem::stop(void)
{
	signal(SIGINT, SIG_DFL);
	INFO(log_) << "Stopping event system.";
	stop_ = true;
}

void
EventSystem::reload(void)
{
	/* XXX Need a way to perform all that allows new handlers to be installed.  */
	INFO(log_) << "Running reload handlers.";
	while (!interest_queue_[EventInterestReload].empty())
		interest_queue_[EventInterestReload].perform();
	INFO(log_) << "Reload handlers have been run.";
}

static void
signal_reload(int)
{
	EventSystem::instance()->reload();
}

static void
signal_stop(int)
{
	EventSystem::instance()->stop();
}
