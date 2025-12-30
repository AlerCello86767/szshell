/*
 * szShell(szsh)
 * 支持基础命令、历史记录、目录操作和root权限执行
 * 使用C++17标准编写
 */
// 标准库头文件
#include <iostream>      // 输入输出流
#include <vector>       // 动态数组容器
#include <map>          // 关联容器
#include <functional>   // 函数对象支持
#include <fstream>

// 系统调用头文件
#include <unistd.h>     // POSIX API(文件/目录操作)
#include <sys/stat.h>   // 文件状态检查
#include <dirent.h>     // 目录操作
#include <cstdlib> // 添加getenv支持

// Readline库
#include <readline/readline.h>  // 命令行编辑
#include <readline/history.h>   // 历史记录
#include <unistd.h>
#include <pwd.h>

using namespace std;    // 使用标准命名空间

/* ========== 全局变量 ========== */
string current_dir;     // 当前工作目录路径
vector<string> history; // 命令历史记录
// 命令注册表：命令名称->处理函数(接收参数列表)
map<string, function<void(const vector<string>&)>> commands;
map<string,string> help_list;
string username ;//= get_username();
string hostname ;//= get_hostname();
map<string ,string>config;

int read_config() {
    const std::string configFile = "/data/data/com.termux/files/home/szsh.cfg";
    
    // 默认配置项
    const vector<pair<string, string>> defaultConfig = {
        {"hide_hostname", "false"},
        {"hide_dir", "false"},
        {"version", "0.5.0beta"},
        {"hide_szsh","false"}
        
    };
    // 初始化所有配置项为默认值
    for (const auto& [key, value] : defaultConfig) {
        config[key] = value;
    }
    // 尝试打开文件读取
    std::ifstream inFile(configFile);
    if (inFile) {
        // 文件存在，读取配置
        std::string key, value;
        while (inFile >> key >> value) {
            // 只处理已知的配置项
            if (config.find(key) != config.end()) {
                config[key] = value;  // 保持原始字符串值
            }
        }
        inFile.close();
    } else {
        // 文件不存在，创建并写入默认配置
        std::ofstream outFile(configFile);
        if (!outFile) {
            std::cerr << "无法创建配置文件" << std::endl;
            return 1;
        }
        // 写入默认配置
        for (const auto& [key, value] : defaultConfig) {
            outFile << key << " " << value << "\n";
        }
    }
    return 0;
}

/* ========== 工具函数 ========== */
string format_path(const string& path) {
    string home_path = getenv("HOME") ? getenv("HOME") : "/data/data/com.termux/files/home";
    if (path.find(home_path) == 0) {
        return "~" + path.substr(home_path.length());
    }
    return path;
}
std::string get_username() {
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        return std::string(pw->pw_name);
    }
    char *login = getlogin();
    if (login) {
        return std::string(login);
    }
    return "";
}//获取用户名
std::string get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname))) {
        return "";
    }
    return std::string(hostname);
}//获取主机名
/**
 * 检查当前是否root权限
 * @return bool true表示是root用户
 */
bool is_root() {
    return geteuid() == 0;  // geteuid返回有效用户ID
}
/**
 * 以root权限执行命令
 * @param cmd 要执行的命令字符串
 */
void execute_as_root(const string& cmd) {
    // 已经是root则直接执行，否则通过su提权
    string full_cmd = is_root() ? cmd : "su -c \"" + cmd + "\"";
    int result = system(full_cmd.c_str());
    if (result != 0) {
        cerr << "命令运行失败，错误码: " << result << endl;
    }
}
/**
 * 更新当前目录缓存
 */
void update_current_dir() {
    char* dir = getcwd(nullptr, 0);  // 获取当前目录
    if (dir) {
        current_dir = dir;    // 更新缓存
        free(dir);            // 释放内存
    } else {
        perror("getcwd error");
        current_dir = "[未知]";  // 错误处理
    }
}
/*string home_set(string current_dir){
    
    return current_dir; // 跳过已替换部分
    }
}*/

/* ========== 命令实现 ========== */
/*
 * 注册所有支持的命令
 */
void register_commands() {
    update_current_dir();  // 初始化目录缓存
    // 退出命令
    commands["exit"] = [](const vector<string>&) {
        cout << "sz终端已退出" << endl;
        exit(0);  // 正常退出
        help_list["exit"]="exit :退出终端";
    };
    // 历史记录命令
    commands["history"] = [](const vector<string>&) {
        cout << "命令历史:" << endl;
        for (size_t i = 0; i < history.size(); i++) {
            // 显示序号和命令
            cout << " " << i+1 << ": " << history[i] << endl;
        }
        help_list["history"]="history:查看历史";
    };
    // 列出目录内容
    commands["ls"] = [](const vector<string>& args) {
        // 默认当前目录，可指定其他目录
            string path = args.size() > 1 ? args[1] : ".";
        
            DIR* dir = opendir(path.c_str());  // 打开目录
            if (!dir) {
                perror("ls 错误");
                return;
            }
            // 遍历目录项
            dirent* entry;
            while ((entry = readdir(dir))) {
            // 跳过隐藏文件(以.开头)
                if (entry->d_name[0] != '.') {
                    cout << entry->d_name << "\t";
                }
            }
            cout << endl;
            closedir(dir);  // 关闭目录
        };
        // 切换目录
        commands["cd"] = [](const vector<string>& args) {
        if (args.size() < 2) {
            cerr << "用法: cd <目录>" << endl;
            return;
        }
        if (chdir(args[1].c_str()) != 0) {  // 尝试切换目录
        perror("cd 错误");
        }
        else {
            update_current_dir();  // 成功则更新缓存
            current_dir = format_path(current_dir);  // 格式化路径显示
        }
    };
    
    // root权限执行
    commands["root"] = [](const vector<string>& args) {
        if (args.size() < 2) {
            cerr << "用法 :root <命令>" << endl;
            return;
        }
        
        // 拼接命令参数
        string cmd;
        for (size_t i = 1; i < args.size(); i++) {
            if (i > 1) cmd += " ";
            cmd += args[i];
        }
        
        execute_as_root(cmd);  // 提权执行
    };
    // 在 register_commands() 函数中添加：
    commands["run"] = [](const vector<string>& args) {
         if (args.size() < 2) {
            cerr << "用法: run <指令> [参数...]" << endl;
            return;
        }
    
        // 构建完整命令路径
        string full_path = args[1];
        if (full_path.find('/') == string::npos) {
            // 如果不是绝对路径，尝试在当前目录查找
            full_path = "./" + full_path;
        }

        // 检查文件是否存在且可执行
        struct stat st;
        if (stat(full_path.c_str(), &st) != 0 || !(st.st_mode & S_IXUSR)) {
            cerr << "错误 : 文件不存在或无法执行"<<endl;
            return;
    }

    // 构建参数列表
        string cmd;
        for (size_t i = 1; i < args.size(); i++) {
            if (i > 1) cmd += " ";
            cmd += args[i];
        }

    // 执行程序
        int result = system(cmd.c_str());
        if (result != 0) {
            cerr << "执行失败，错误码：" << result << endl;
        }
    };
    
    // 显示当前目录
    commands["pwd"] = [](const vector<string>&) {
        cout << current_dir << endl;
    };
    
    // 清除缓冲区命令
    commands["clear"] = [](const vector<string>&) {
        cout << "\ec\e[3J";
        
    };
    commands["help"] = [](const vector<string>& args) {
        if (args.size() > 1) {
            // 显示特定命令的帮助
            if (help_list.count(args[1])) {
                cout << help_list[args[1]] << endl;
            } else {
                cout << "没有关于";
            }
        }
    };
    commands["whoami"] = [](const vector<string>&) {
        cout<<get_username()<<endl;
    };
    commands["version"] = [](const vector<string>&) {
        cout << "szShell"<<endl;
        cout << config["version"]<<endl;
        cout <<"by cai"<<endl;
        
    };
    // 在register_commands()函数中添加echo命令
commands["echo"] = [](const vector<string>& args) {
    if (args.size() < 2) {
        cout << endl;  // 如果没有参数，只输出换行
        return;
    }
    
    // 从第二个参数开始拼接所有参数
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) cout << " ";  // 参数间添加空格
        cout << args[i];
    }
    cout << endl;  // 最后输出换行
    
    // 添加到帮助列表
    help_list["echo"] = "echo [text...]: 输出指定的文本";
};
    //调用sh
    commands["sh"] = [](const vector<string>& args) {
    string cmd;
    
    // 在 Termux 中，默认使用 bash 而不是 sh
    if (args.size() < 2) {
        // 没有参数时，启动交互式 shell
        system("bash");
        return;
    }
    
    // 构建命令字符串
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) cmd += " ";
        cmd += args[i];
    }
    
    // 在 Termux 中执行命令
    string full_cmd = "bash -c \"" + cmd + "\"";
    int result = system(full_cmd.c_str());
    
    if (result != 0) {
        cerr << "命令执行失败，错误码: " << result << endl;
    }
    
    help_list["sh"] = "sh [command]: 执行 shell 命令\n  无参数: 启动交互式 shell\n  带参数: 执行指定命令";
    };
}
/**
 * 分割命令行参数
 * @param input 原始输入字符串
 * @return 分割后的参数列表
 */
vector<string> split_args(const string& input) {
    vector<string> args;
    string arg;
    bool in_quote = false;  // 是否在引号中
    
    for (char c : input) {
        if (c == '"') {
            in_quote = !in_quote;  // 切换引号状态
        } else if (isspace(c) && !in_quote) {
            // 遇到空格且不在引号中时分割参数
            if (!arg.empty()) {
                args.push_back(arg);
                arg.clear();
            }
        } else {
            arg += c;  // 构建当前参数
        }
    }
    
    // 添加最后一个参数
    if (!arg.empty()) {
        args.push_back(arg);
    }
    
    return args;
}

/* ========== 主程序 ========== */

int main(int argc, char* argv[]) {
    // 初始化命令系统
    read_config();
    username = get_username();
    hostname = get_hostname();
    register_commands();
    string home_path="/data/data/com.termux/files/home";
    string new_str="~";
    size_t pos = 0;
    
    if (argc > 1) {
        ifstream script(argv[1]);
        if (!script) {
            cerr << "无法打开脚本: " << argv[1] << endl;
            return 1;
        }

        string line;
        while (getline(script, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            add_history(line.c_str());
            history.push_back(line);
            
            vector<string> args = split_args(line);
            if (!args.empty() && commands.count(args[0])) {
                commands[args[0]](args);
            }
        }
        return 0;
    }
    
    // 主循环
    while (true) {
        while ((pos = current_dir.find(home_path, pos)) != std::string::npos) {
            current_dir.replace(pos, home_path.length(), new_str);
            pos += new_str.length();
            }
            //cout<<"\e[34m["<<current_dir<<"]\e[0m";
        cout<<"\e[1;32m"<<username<<"\e[0m";
        if(config["hide_hostname"]=="false"){
            cout<<"\e[1;32m@"<<hostname<<"\e[0m";
        }
        if(config["hide_dir"]=="false"){
            cout<<"\e[34m["<<current_dir<<"]\e[0m";
        }
        // 构造提示符(root显示#，普通用户显示$)
        cout<<"\n";
        if(config["hide_szsh"]=="false"){
            cout<<"szsh";
        }
        
        string prompt = is_root() ? "# " : "$ ";
        char* input = readline(prompt.c_str());  // 获取输入
        
        if (!input) break;  // 处理EOF(Ctrl+D)
        
        string cmd(input);
        free(input);  // 释放readline分配的内存
        
        if (cmd.empty()) continue;  // 跳过空输入
        
        // 记录历史
        add_history(cmd.c_str());
        history.push_back(cmd);
            
        // 解析参数
        vector<string> args = split_args(cmd);
        if (args.empty()) continue;
        
        // 执行命令
        if (commands.count(args[0])) {
            commands[args[0]](args);  // 执行注册命令
        } else {
            cerr<<"未知指令"<<endl;
        }
        cout<<"\n";
    }
    
    return 0;  // 正常退出
}