#ifndef COMMAND_H
#define COMMAND_H

#include <string>
#include <iostream>
#include <vector>

using namespace std;

class Command
{
public:
    Command(string str):_command(str) {}
    ~Command() {}
    bool is_valid_command();
    string get_command();
    void split_command();
    void print_error(int);
    vector<string> _arg;

private:
    string _command;
    const string _all_order[8] = { "set","get","del","load","dump","show","clear","quit"};
    const int _order_number = 8;
    string delimiter2 = " ";
};
#endif