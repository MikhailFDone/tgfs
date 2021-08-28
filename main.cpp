#define FUSE_USE_VERSION 26

#include <fuse.h>

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <iostream>

static struct fuse_operations fuse_example_operations = {};

namespace td_api = td::td_api;

class TdExample
{
public:
	TdExample()
	{
		td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
		client_manager_ = std::make_unique<td::ClientManager>();
		client_id_ = client_manager_->create_client_id();
	}

private:
	using Object = td_api::object_ptr<td_api::Object>;
	std::unique_ptr<td::ClientManager> client_manager_;
	std::int32_t client_id_{0};
};

int main(int argc, char *argv[])
{
	std::cout << "HELLO" << std::endl;
	return fuse_main(argc, argv, &fuse_example_operations, NULL);
}
