#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H
#include <chrono>
#include <mutex>
#include <list>
#include <condition_variable>

#include <boost/noncopyable.hpp>

/*
TODO:
- this is an blocking queue, consider after updating to a newer boost version
the lockfree queues.

Idea for lockfree pop_wait:
keep the conditional and lock members
void push(Message const& msg)
{
   _queue.push_back(msg);
   _notifier.notify_one(); // no lock only signal the state
}

void pop_wait(Message& msg)
{
 if (try_pop(msg)) // fast lookup
  return;
 // queue is empty part
 boost::mutex::scoped_lock lock(_mutex);
 while (!try_pop(msg)) 
 {
  t= some time (a few ms)
  // timed wait just to be sure that no deadlocks can accur
  // since between the try_pop a new entry can be inserted
  _notifier.timed_wait(lock, t);                            
 }
}
*/
namespace Pol {
  namespace Clib {
	template <typename Message>
	class message_queue : boost::noncopyable
	{
	public:
	  message_queue();
	  ~message_queue();

	  // push new message into queue and notify possible wait_pop
	  void push( Message const& msg );
	  void push( std::list<Message>& msg_list );
	  // push new message into queue and notify possible wait_pop
	  // will move the msg into the queue, thus the reference is likely to be
	  // invalid afterwards
	  void push_move( Message&& msg );
	  
	  // check if empty (a bit senseless)
	  bool empty() const;
	  // return current size (unsafe aka senseless)
	  std::size_t size() const;

	  // tries to get a message true on success false otherwise
	  bool try_pop( Message* msg );
	  // waits till queue is non empty
	  void pop_wait( Message* msg );
	  // waits till queue is non empty and fill list
	  void pop_wait( std::list<Message>* msgs );
	  // empties the queue (unsafe)
	  void pop_remaining( std::list<Message>* msgs );

	  void cancel();
	  struct Canceled
	  {};
	 private:
	  std::list<Message> _queue;
	  mutable std::mutex _mutex;
	  std::condition_variable _notifier;
	  bool _cancel;
	};

	template <typename Message>
	message_queue<Message>::message_queue() :
	  _queue(), _mutex(), _notifier(), _cancel( false )
	{}

	template <typename Message>
	message_queue<Message>::~message_queue()
	{
	  cancel();
	}

	template <typename Message>
	void message_queue<Message>::push( Message const& msg )
	{
	  std::list<Message> tmp;
	  tmp.push_back( msg );  // costly pushback outside the lock
	  {
		std::lock_guard<std::mutex> lock( _mutex );
		_queue.splice( _queue.end(), tmp );  // fast splice inside
		_notifier.notify_one();
	  }
	}

	template <typename Message>
	void message_queue<Message>::push_move( Message&& msg )
	{
	  std::list<Message> tmp;
	  tmp.emplace_back(
		std::move( msg ) );  // costly pushback outside the lock
	  {
		std::lock_guard<std::mutex> lock( _mutex );
		_queue.splice( _queue.end(), tmp );  // fast splice inside
		_notifier.notify_one();
	  }
	}

    template <typename Message>
	void message_queue<Message>::push( std::list<Message>& msg_list )
    {
      std::lock_guard<std::mutex> lock( _mutex );
      _queue.splice( _queue.end( ), msg_list );  // fast splice inside
      _notifier.notify_one( );
    }

	template <typename Message>
	bool message_queue<Message>::empty() const
	{
	  std::lock_guard<std::mutex> lock( _mutex );
	  return _queue.empty();
	}

	template <typename Message>
	std::size_t message_queue<Message>::size() const
	{
	  std::lock_guard<std::mutex> lock( _mutex );
	  return _queue.size();
	}

	/// tries to get a message true on success false otherwise
	template <typename Message>
	bool message_queue<Message>::try_pop( Message* msg )
	{
	  std::lock_guard<std::mutex> lock( _mutex );
	  if( _queue.empty() ) return false;
	  *msg = std::move( _queue.front() );
	  _queue.pop_front();
	  return true;
	}

	template <typename Message>
	void message_queue<Message>::pop_wait( Message* msg )
	{
	  std::unique_lock<std::mutex> lock( _mutex );
	  while( _queue.empty() && !_cancel )
		_notifier.wait( lock );  // will unlock mutex during wait
	  if( _cancel ) throw Canceled();
	  *msg = std::move( _queue.front() );
	  _queue.pop_front();
	}

	template <typename Message>
	void message_queue<Message>::pop_wait( std::list<Message>* msgs )
	{
	  std::unique_lock<std::mutex> lock( _mutex );
	  while( _queue.empty() && !_cancel )
		_notifier.wait( lock );  // will unlock mutex during wait
	  if( _cancel ) throw Canceled();
	  msgs->splice( msgs->end(), _queue );
	}

	template <typename Message>
	void message_queue<Message>::pop_remaining( std::list<Message>* msgs )
	{
	  msgs->splice( msgs->end(), _queue );
	}
	template <typename Message>
	void message_queue<Message>::cancel()
	{
	  std::lock_guard<std::mutex> lock( _mutex );
	  _cancel = true;
	  _notifier.notify_all();
	}
  }
}

#endif