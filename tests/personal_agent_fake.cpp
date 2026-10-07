#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

using Json=nlohmann::json;
int main(int argc,char** argv)
{
    std::vector<std::string> args;
    for(int i=1;i<argc;++i) args.emplace_back(argv[i]);
    {std::ofstream file("launch.json");file<<Json(args).dump();}
    if(std::find(args.begin(),args.end(),"app-server")==args.end()) return 2;
    auto emit=[](const Json& j){std::cout<<j.dump()<<'\n'<<std::flush;};
    auto notification=[&](std::string method,Json params){params["threadId"]="fake-thread";emit({{"method",method},{"params",params}});};
    std::string line,turn;
    int index=0;
    auto complete=[&](std::string status){notification("turn/completed",{{"turn",{{"id",turn},{"status",status}}}});};
    while(std::getline(std::cin,line))
    {
        const auto request=Json::parse(line);
        {std::ofstream file("received.jsonl",std::ios::app);file<<request.dump()<<'\n';}
        const auto method=request.value("method","");
        const auto id=request.value("id",Json());
        if(method=="initialize") emit({{"id",id},{"result",Json::object()}});
        else if(method=="thread/start" || method=="thread/resume")
        {
            if(method=="thread/resume" && !std::filesystem::exists("fake-rollout"))
                emit({{"id",id},{"error",{{"code",-32600},{"message","no rollout found for thread id fake-thread"}}}});
            else emit({{"id",id},{"result",{{"thread",{{"id","fake-thread"},{"turns",Json::array()}}}}}});
        }
        else if(method=="turn/start")
        {
            {std::ofstream durable("fake-rollout");durable<<"first turn persisted";}
            turn="turn-"+std::to_string(++index);
            emit({{"id",id},{"result",{{"turn",{{"id",turn},{"status","inProgress"}}}}}});
            notification("turn/started",{{"turn",{{"id",turn}}}});
            const auto text=request["params"]["input"][0]["text"].get<std::string>();
            if(text=="crash") return 7;
            if(text=="malformed") {std::cout<<"not JSON\n"<<std::flush;continue;}
            if(text=="wait") continue;
            if(text=="permissions")
            {
                const Json entries=Json::array({{{"access","read"},
                    {"path",{{"type","path"},{"path",std::filesystem::current_path().string()}}}}});
                const Json permissions{{"network",{{"enabled",true}}},{"fileSystem",{{"entries",entries}}}};
                emit({{"id","permissions-"+turn},{"method","item/permissions/requestApproval"},
                    {"params",{{"threadId","fake-thread"},{"turnId",turn},{"itemId","permission-item"},
                        {"cwd","."},{"startedAtMs",0},{"reason","Read the requested folder and fetch its data"},
                        {"permissions",permissions}}}});continue;
            }
            if(text=="approval") {emit({{"id","approval-1"},{"method","item/commandExecution/requestApproval"},{"params",{{"threadId","fake-thread"},{"turnId",turn},{"command","echo approved"},{"cwd","."}}}});continue;}
            notification("item/started",{{"item",{{"type","agentMessage"},{"id","commentary-"+turn},{"phase","commentary"},{"text","I will calculate things"}}}});
            notification("item/completed",{{"item",{{"type","agentMessage"},{"id","commentary-"+turn},{"phase","commentary"},{"text","I will calculate things"}}}});
            notification("item/started",{{"item",{{"type","commandExecution"},{"id","tool-"+turn},{"command","secret calculation"}}}});
            notification("item/started",{{"item",{{"type","reasoning"},{"id","reasoning-"+turn},{"text","hidden reasoning"}}}});
            std::string answer="Answer: "+text;
            if(request["params"].contains("outputSchema"))
            {
                const bool update=std::filesystem::exists("due.txt");
                answer=Json{{"has_update",update},{"message",update?"Scheduled work completed.":""}}.dump();
                if(std::filesystem::exists("invalid-schedule.txt")) answer="invalid scheduled response";
                std::ofstream check("checks.jsonl",std::ios::app);check<<request.dump()<<'\n';
            }
            notification("item/started",{{"item",{{"type","agentMessage"},{"id","answer-"+turn},{"phase","final_answer"},{"text",""}}}});
            notification("item/agentMessage/delta",{{"itemId","answer-"+turn},{"turnId",turn},{"delta",answer}});
            notification("item/completed",{{"item",{{"type","agentMessage"},{"id","answer-"+turn},{"phase","final_answer"},{"text",answer}}}});
            complete("completed");
        }
        else if(method=="turn/interrupt") {emit({{"id",id},{"result",Json::object()}});complete("interrupted");}
        else if(request.contains("result") && (id=="approval-1" || (id.is_string() && id.get<std::string>().starts_with("permissions-")))) complete("completed");
    }
}
