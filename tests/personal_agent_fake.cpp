#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc,char** argv)
{
    std::vector<std::string> args;
    for(int i=1;i<argc;++i) args.emplace_back(argv[i]);
    { std::ofstream file("launch.json"); file << nlohmann::json(args).dump(); }
    std::cout << "PERSONAL_CHAT_READY\n" << std::flush;
    std::string text;
    while(std::getline(std::cin,text))
    {
        { std::ofstream file("received.txt",std::ios::app); file << text << '\n'; }
        if(text=="quit") break;
        std::cout << "REPLY: " << text << '\n' << std::flush;
    }
}
