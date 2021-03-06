#include <cstring>
#include <fstream>
#include <iostream>

#include "err_constants.h"
#include "example_joiner.h"
#include "joined_log_parser.h"

namespace v2 = reinforcement_learning::messages::flatbuff::v2;
namespace err = reinforcement_learning::error_code;

// we could add any joiner logic that we want and have a flag or parameter that
// desides which method to select
int non_default_reward_calc(const joined_event &event) {
  std::cout << "this is a different reward logic that does nothing"
            << std::endl;

  return err::success;
}

JoinedLogParser::JoinedLogParser(const std::string &initial_command_line)
    : example_joiner(VW::make_unique<ExampleJoiner>(initial_command_line,
                                                    non_default_reward_calc)) {}

JoinedLogParser::~JoinedLogParser() = default;

// TODO make better error messages
// TODO check endianness
int JoinedLogParser::read_and_deserialize_file(const std::string &file_name) {
  std::ifstream fs(file_name.c_str(), std::ifstream::binary);
  if (fs.fail()) {return err::file_read_error;}
  std::vector<char> buffer(4, 0);
  const std::vector<char> magic = {'V', 'W', 'F', 'B'};
  // read the 4 magic bytes
  fs.read(buffer.data(), buffer.size());
  if (fs.fail()) {return err::file_read_error;}
  if (buffer != magic) {
    return err::file_read_error;
  }

  // read the version
  fs.read(buffer.data(), buffer.size());
  if (fs.fail()) {return err::file_read_error;}
  if (static_cast<int>(buffer[0]) != 1) {
    return err::file_read_error;
  }

  // payload type, check for header
  unsigned int payload_type;
  fs.read((char *)(&payload_type), sizeof(payload_type));
  if (fs.fail()) {return err::file_read_error;}
  if (payload_type != MSG_TYPE_HEADER) {
    return err::file_read_error;
  }

  // read header size
  fs.read(buffer.data(), buffer.size());
  if (fs.fail()) {return err::file_read_error;}
  uint32_t payload_size = *reinterpret_cast<const uint32_t *>(buffer.data());
  // read the payload
  std::vector<char> payload(payload_size, 0);
  fs.read(payload.data(), payload.size());
  if (fs.fail()) {return err::file_read_error;}

  read_header(payload);

  fs.read((char *)(&payload_type), sizeof(payload_type));
  if (fs.fail()) {return err::file_read_error;}
  while (payload_type != MSG_TYPE_EOF) {
    if (payload_type != MSG_TYPE_REGULAR)
      return err::file_read_error;
    // read payload size
    fs.read(buffer.data(), buffer.size());
    if (fs.fail()) {return err::file_read_error;}
    uint32_t payload_size = *reinterpret_cast<const uint32_t *>(buffer.data());
    // read the payload
    std::vector<char> payload(payload_size, 0);
    fs.read(payload.data(), payload.size());
    if (fs.fail()) {return err::file_read_error;}
    if (read_message(payload) != err::success)
      return err::file_read_error;
    fs.read((char *)(&payload_type), sizeof(payload_type));
    if (fs.fail()) {return err::file_read_error;}
  }

  return err::success;
}

int JoinedLogParser::read_header(const std::vector<char> &payload) {

  // TODO pass header info into the ExampleJoiner object

  auto file_header = v2::GetFileHeader(payload.data());
  std::cout << "header properties:" << std::endl;
  for (size_t i = 0; i < file_header->properties()->size(); i++) {
    std::cout << file_header->properties()->Get(i)->key()->c_str() << ":"
              << file_header->properties()->Get(i)->value()->c_str()
              << std::endl;
  }
  return err::success;
}

int JoinedLogParser::read_message(const std::vector<char> &payload) {
  auto joined_payload = flatbuffers::GetRoot<v2::JoinedPayload>(payload.data());
  for (size_t i = 0; i < joined_payload->events()->size(); i++) {
    // process events one-by-one
    example_joiner->process_event(*joined_payload->events()->Get(i));
  }
  example_joiner->train_on_joined();
  return err::success;
}
