#ifndef __REQUEST_H
#define __REQUEST_H

#include <vector>
#include <functional>
#include <cstdint>

using namespace std;

namespace ramulator
{

class Request
{
public:
    bool is_first_command = true;
    bool is_pim = false;
    uint8_t optype = 0;
    int batch_id = 0;
    long addr = -1;
    vector<int> addr_vec;
    // specify which core this request sent from, for virtual address translation
    int coreid = 0;

    enum class Type
    {
        READ,
        WRITE,
        REFRESH,
        POWERDOWN,
        SELFREFRESH,
        EXTENSION,
        PIM_READ,
        PIM_WRITE,
        MAX
    } type;

    bool is_pim_req() const { return is_pim || type == Type::PIM_READ || type == Type::PIM_WRITE; }

    long arrive = -1;
    long depart = -1;
    function<void(Request&)> callback; // call back with more info

    Request(long addr, Type type, int coreid = 0)
        : is_first_command(true),
          is_pim(type == Type::PIM_READ || type == Type::PIM_WRITE),
          optype(0),
          batch_id(0),
          addr(addr),
          coreid(coreid),
          type(type),
          arrive(-1),
          depart(-1),
          callback([](Request& req){}) {}

    Request(long addr, Type type, function<void(Request&)> callback, int coreid = 0)
        : is_first_command(true),
          is_pim(type == Type::PIM_READ || type == Type::PIM_WRITE),
          optype(0),
          batch_id(0),
          addr(addr),
          coreid(coreid),
          type(type),
          arrive(-1),
          depart(-1),
          callback(callback) {}

    Request(const vector<int>& addr_vec, Type type, function<void(Request&)> callback, int coreid = 0)
        : is_first_command(true),
          is_pim(type == Type::PIM_READ || type == Type::PIM_WRITE),
          optype(0),
          batch_id(0),
          addr(-1),
          addr_vec(addr_vec),
          coreid(coreid),
          type(type),
          arrive(-1),
          depart(-1),
          callback(callback) {}

    Request()
        : is_first_command(true),
          is_pim(false),
          optype(0),
          batch_id(0),
          addr(-1),
          coreid(0),
          type(Type::READ),
          arrive(-1),
          depart(-1),
          callback([](Request& req){}) {}
};

} /*namespace ramulator*/

#endif /*__REQUEST_H*/

