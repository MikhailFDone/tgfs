#define FUSE_USE_VERSION 29

#include <fuse.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

class TgfsFile
{

};

class TgfsDir
{
	std::map<std::string, TgfsFile> _files;

public:
	void GetAttr(struct stat *stbuf) const
	{
		memset(stbuf, 0, sizeof(struct stat));

		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}
};

class TgfsRoot
{
	std::map<std::string, TgfsDir> _dirs;

	public:

	size_t Size() const { return _dirs.size(); }

	void AddDirectory(std::string dir_name)
	{
		// TODO: что будет, если у чатов одинаковые названия?
		_dirs.emplace(dir_name, TgfsDir{});
	}

	int GetAttr(std::string_view path, struct stat *stbuf) const
	{	
		std::cerr << "GetAttr " << path << std::endl;

		int res = 0;

		if (path == "/")
		{
			GetAttr(stbuf);
		}
		// TODO: speed up
		else if (auto it = _dirs.find(std::string(path.substr(1))); it != _dirs.end() && path[0] == '/')
		{
			it->second.GetAttr(stbuf);
		}
		else
		{
			res = -ENOENT;
		}

		return res;	
	}

	int ReadDir(std::string_view path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi) const
	{
		(void) fi;

		std::cerr << "OFFSET " << offset << std::endl;

		if (path != "/")
			return -ENOENT;

		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		
		for (auto dir : _dirs)
		{
			if (dir.first.empty())
			{
				std::cerr << "EMPTY STRING!!!" << std::endl;
				continue;
			}
			std::cerr << dir.first << std::endl;
			filler(buf, dir.first.c_str(), NULL, 0);
		}
		
		return 0;
	}	

private:

	void GetAttr(struct stat* stbuf) const
	{
		memset(stbuf, 0, sizeof(struct stat));

		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;	
	}
};

// overloaded
namespace detail
{
	template <class... Fs>
	struct overload;

	template <class F>
	struct overload<F> : public F
	{
		explicit overload(F f) : F(f)
		{
		}
	};
	template <class F, class... Fs>
	struct overload<F, Fs...>
		: public overload<F>, public overload<Fs...>
	{
		overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...)
		{
		}
		using overload<F>::operator();
		using overload<Fs...>::operator();
	};
} // namespace detail

template <class... F>
auto overloaded(F... f)
{
	return detail::overload<F...>(f...);
}

namespace td_api = td::td_api;

class TdExample
{
	TgfsRoot _tgfs_root;

public:
	TdExample()
	{
		td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
		client_manager_ = std::make_unique<td::ClientManager>();
		client_id_ = client_manager_->create_client_id();
		send_query(td_api::make_object<td_api::getOption>("version"), {});
	}

	const TgfsRoot& GetTgfs() const
	{
		return _tgfs_root;
	}

	void Login(int argc, char *argv[])
	{
		while (!are_authorized_)
		{
			process_response(client_manager_->receive(10));
		}
	}

	void Update()
	{
		std::cout << "Checking for updates..." << std::endl;
		while (true)
		{
			auto response = client_manager_->receive(0);
			if (response.object)
			{
				process_response(std::move(response));
			}
			else
			{
				break;
			}
		}
	}

	void GetAllDirectories()
	{
		std::cout << "Loading chat list..." << std::endl;
		send_query(td_api::make_object<td_api::getChats>(nullptr, std::numeric_limits<std::int64_t>::max(), 0, 20),
				   [this](Object object)
				   {
					   if (object->get_id() == td_api::error::ID)
					   {
						   return;
					   }
					   auto chats = td::move_tl_object_as<td_api::chats>(object);
					   for (auto chat_id : chats->chat_ids_)
					   {
						   std::cout << "[chat_id:" << chat_id << "] [title:" << chat_title_[chat_id] << "]" << std::endl;
					   }
				   });
	}

	void loop(int argc, char *argv[])
	{
		while (true)
		{
			if (need_restart_)
			{
				restart();
			}
			else
			{
				std::cout << "Enter action [q] quit [u] check for updates and request results [c] show chats [m <chat_id> "
							 "<text>] send message [me] show self [l] logout: "
						  << std::endl;
				std::string line;
				std::getline(std::cin, line);
				std::istringstream ss(line);
				std::string action;
				if (!(ss >> action))
				{
					continue;
				}
				if (action == "q")
				{
					return;
				}
				if (action == "u")
				{
					Update();
				}
				else if (action == "close")
				{
					std::cout << "Closing..." << std::endl;
					send_query(td_api::make_object<td_api::close>(), {});
				}
				else if (action == "me")
				{
					send_query(td_api::make_object<td_api::getMe>(),
							   [this](Object object)
							   { std::cout << to_string(object) << std::endl; });
				}
				else if (action == "l")
				{
					std::cout << "Logging out..." << std::endl;
					send_query(td_api::make_object<td_api::logOut>(), {});
				}
				else if (action == "m")
				{
					std::int64_t chat_id;
					ss >> chat_id;
					ss.get();
					std::string text;
					std::getline(ss, text);

					std::cout << "Sending message to chat " << chat_id << "..." << std::endl;
					auto send_message = td_api::make_object<td_api::sendMessage>();
					send_message->chat_id_ = chat_id;
					auto message_content = td_api::make_object<td_api::inputMessageText>();
					message_content->text_ = td_api::make_object<td_api::formattedText>();
					message_content->text_->text_ = std::move(text);
					send_message->input_message_content_ = std::move(message_content);

					send_query(std::move(send_message), {});
				}
				else if (action == "c")
				{
					std::cout << "Loading chat list..." << std::endl;
					send_query(td_api::make_object<td_api::getChats>(nullptr, std::numeric_limits<std::int64_t>::max(), 0, 20),
							   [this](Object object)
							   {
								   if (object->get_id() == td_api::error::ID)
								   {
									   return;
								   }
								   auto chats = td::move_tl_object_as<td_api::chats>(object);
								   for (auto chat_id : chats->chat_ids_)
								   {
									   _tgfs_root.AddDirectory(chat_title_[chat_id]);
									   std::cout << "[chat_id:" << chat_id << "] [title:" << chat_title_[chat_id] << "]" << std::endl;
								   }
							   });
				}
				else if (action == "fs")
				{
					startFS(argc, argv);
				}
			}
		}
	}

private:
	using Object = td_api::object_ptr<td_api::Object>;
	std::unique_ptr<td::ClientManager> client_manager_;
	std::int32_t client_id_{0};

	td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
	bool are_authorized_{false};
	bool need_restart_{false};
	std::uint64_t current_query_id_{0};
	std::uint64_t authentication_query_id_{0};

	std::map<std::uint64_t, std::function<void(Object)>> handlers_;

	std::map<std::int32_t, td_api::object_ptr<td_api::user>> users_;

	std::map<std::int64_t, std::string> chat_title_;

	void restart()
	{
		client_manager_.reset();
		*this = TdExample();
	}

	void send_query(td_api::object_ptr<td_api::Function> f, std::function<void(Object)> handler)
	{
		auto query_id = next_query_id();
		if (handler)
		{
			handlers_.emplace(query_id, std::move(handler));
		}
		client_manager_->send(client_id_, query_id, std::move(f));
	}

	void process_response(td::ClientManager::Response response)
	{
		if (!response.object)
		{
			return;
		}
		//std::cout << response.request_id << " " << to_string(response.object) << std::endl;
		if (response.request_id == 0)
		{
			return process_update(std::move(response.object));
		}
		auto it = handlers_.find(response.request_id);
		if (it != handlers_.end())
		{
			it->second(std::move(response.object));
			handlers_.erase(it);
		}
	}

	std::string get_user_name(std::int32_t user_id) const
	{
		auto it = users_.find(user_id);
		if (it == users_.end())
		{
			return "unknown user";
		}
		return it->second->first_name_ + " " + it->second->last_name_;
	}

	std::string get_chat_title(std::int64_t chat_id) const
	{
		auto it = chat_title_.find(chat_id);
		if (it == chat_title_.end())
		{
			return "unknown chat";
		}
		return it->second;
	}

	void process_update(td_api::object_ptr<td_api::Object> update)
	{
		td_api::downcast_call(
			*update, overloaded(
						 [this](td_api::updateAuthorizationState &update_authorization_state)
						 {
							 authorization_state_ = std::move(update_authorization_state.authorization_state_);
							 on_authorization_state_update();
						 },
						 [this](td_api::updateNewChat &update_new_chat)
						 {
							 chat_title_[update_new_chat.chat_->id_] = update_new_chat.chat_->title_;
							 _tgfs_root.AddDirectory(update_new_chat.chat_->title_);
						 },
						 [this](td_api::updateChatTitle &update_chat_title)
						 {
							 chat_title_[update_chat_title.chat_id_] = update_chat_title.title_;
						 },
						 [this](td_api::updateUser &update_user)
						 {
							 auto user_id = update_user.user_->id_;
							 users_[user_id] = std::move(update_user.user_);
						 },
						 [this](td_api::updateNewMessage &update_new_message)
						 {
							 auto chat_id = update_new_message.message_->chat_id_;
							 std::string sender_name;
							 td_api::downcast_call(*update_new_message.message_->sender_,
												   overloaded(
													   [this, &sender_name](td_api::messageSenderUser &user)
													   {
														   sender_name = get_user_name(user.user_id_);
													   },
													   [this, &sender_name](td_api::messageSenderChat &chat)
													   {
														   sender_name = get_chat_title(chat.chat_id_);
													   }));
							 std::string text;
							 if (update_new_message.message_->content_->get_id() == td_api::messageText::ID)
							 {
								 text = static_cast<td_api::messageText &>(*update_new_message.message_->content_).text_->text_;
							 }
							 std::cout << "Got message: [chat_id:" << chat_id << "] [from:" << sender_name << "] [" << text
									   << "]" << std::endl;
						 },
						 [](auto &update) {}));
	}

	auto create_authentication_query_handler()
	{
		return [this, id = authentication_query_id_](Object object)
		{
			if (id == authentication_query_id_)
			{
				check_authentication_error(std::move(object));
			}
		};
	}

	void on_authorization_state_update()
	{
		authentication_query_id_++;
		td_api::downcast_call(
			*authorization_state_,
			overloaded(
				[this](td_api::authorizationStateReady &)
				{
					are_authorized_ = true;
					std::cout << "Got authorization" << std::endl;
				},
				[this](td_api::authorizationStateLoggingOut &)
				{
					are_authorized_ = false;
					std::cout << "Logging out" << std::endl;
				},
				[this](td_api::authorizationStateClosing &)
				{ std::cout << "Closing" << std::endl; },
				[this](td_api::authorizationStateClosed &)
				{
					are_authorized_ = false;
					need_restart_ = true;
					std::cout << "Terminated" << std::endl;
				},
				[this](td_api::authorizationStateWaitCode &)
				{
					std::cout << "Enter authentication code: " << std::flush;
					std::string code;
					std::cin >> code;
					send_query(td_api::make_object<td_api::checkAuthenticationCode>(code),
							   create_authentication_query_handler());
				},
				[this](td_api::authorizationStateWaitRegistration &)
				{
					std::string first_name;
					std::string last_name;
					std::cout << "Enter your first name: " << std::flush;
					std::cin >> first_name;
					std::cout << "Enter your last name: " << std::flush;
					std::cin >> last_name;
					send_query(td_api::make_object<td_api::registerUser>(first_name, last_name),
							   create_authentication_query_handler());
				},
				[this](td_api::authorizationStateWaitPassword &)
				{
					std::cout << "Enter authentication password: " << std::flush;
					std::string password;
					std::getline(std::cin, password);
					send_query(td_api::make_object<td_api::checkAuthenticationPassword>(password),
							   create_authentication_query_handler());
				},
				[this](td_api::authorizationStateWaitOtherDeviceConfirmation &state)
				{
					std::cout << "Confirm this login link on another device: " << state.link_ << std::endl;
				},
				[this](td_api::authorizationStateWaitPhoneNumber &)
				{
					std::cout << "Enter phone number: " << std::flush;
					std::string phone_number;
					std::cin >> phone_number;
					send_query(td_api::make_object<td_api::setAuthenticationPhoneNumber>(phone_number, nullptr),
							   create_authentication_query_handler());
				},
				[this](td_api::authorizationStateWaitEncryptionKey &)
				{
					std::cout << "Enter encryption key or DESTROY: " << std::flush;
					std::string key;
					std::getline(std::cin, key);
					if (key == "DESTROY")
					{
						send_query(td_api::make_object<td_api::destroy>(), create_authentication_query_handler());
					}
					else
					{
						send_query(td_api::make_object<td_api::checkDatabaseEncryptionKey>(std::move(key)),
								   create_authentication_query_handler());
					}
				},

				[this](td_api::authorizationStateWaitTdlibParameters &)
				{
					auto parameters = td_api::make_object<td_api::tdlibParameters>();
					parameters->database_directory_ = "tdlib";
					parameters->use_message_database_ = true;
					parameters->use_secret_chats_ = true;
					parameters->api_id_ = 50322;
					parameters->api_hash_ = "9ff1a639196c0779c86dd661af8522ba";
					parameters->system_language_code_ = "en";
					parameters->device_model_ = "Desktop";
					parameters->application_version_ = "1.0";
					parameters->enable_storage_optimizer_ = true;
					send_query(td_api::make_object<td_api::setTdlibParameters>(std::move(parameters)),
							   create_authentication_query_handler());
				}));
	}

	void check_authentication_error(Object object)
	{
		if (object->get_id() == td_api::error::ID)
		{
			auto error = td::move_tl_object_as<td_api::error>(object);
			std::cout << "Error: " << to_string(error) << std::flush;
			on_authorization_state_update();
		}
	}

	std::uint64_t next_query_id()
	{
		return ++current_query_id_;
	}

	public:
		std::vector<std::string> get_chats() const 
		{
			std::vector<std::string> result;
			for (auto chat_title : chat_title_)
				result.push_back(std::string("/") + chat_title.second);
			return result;
		}

		void startFS(int argc, char *argv[]);
};

static TdExample example;

static int hello_getattr(const char *path_, struct stat *stbuf)
{
	std::string_view path = path_;
	return example.GetTgfs().GetAttr(path, stbuf);
}

static int hello_readdir(const char* path_, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	std::string_view path = path_;
	return example.GetTgfs().ReadDir(path, buf, filler, offset, fi);
}

static struct fuse_operations hello_oper;

void TdExample::startFS(int argc, char *argv[])
{
	std::cout << "Dirs count: " << _tgfs_root.Size() << std::endl;

	memset(&hello_oper, 0, sizeof(fuse_operations));
	hello_oper.getattr = hello_getattr;
	hello_oper.readdir = hello_readdir;

	int retcode = fuse_main(argc, argv, &hello_oper, NULL);
	exit(retcode);
}

int main(int argc, char *argv[])
{
	std::cout << "HELLO" << std::endl;

	example.Login(argc, argv);
	example.Update();
	example.GetAllDirectories();
	example.loop(argc, argv);

	return 0;
}
