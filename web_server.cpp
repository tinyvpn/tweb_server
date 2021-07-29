#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <map>
#include <fstream>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <mutex>
#include <mysql/mysql.h>

#include "log.h"
#include "cJSON.h"
#include "stringutl.h"
#include "fileutl.h"
#include "timeutl.h"

extern uint32_t log_level;
std::map<std::string, std::vector<std::string>> g_country_ip_list;
int g_day_limit;
int g_month_limit;

MYSQL *g_mysql_con;

int json_parse_config(const std::string& ret) {
    cJSON* pRoot = cJSON_CreateObject();
    pRoot = cJSON_Parse(ret.c_str());
    std::string country_list = cJSON_GetObjectItem(pRoot, "country")->valuestring;
//    g_obfu = cJSON_GetObjectItem(pRoot, "obfu")->valueint;
    log_level = cJSON_GetObjectItem(pRoot, "log_level")->valueint;
    g_day_limit= htonl(cJSON_GetObjectItem(pRoot, "day_limit")->valueint);
    g_month_limit = htonl(cJSON_GetObjectItem(pRoot, "month_limit")->valueint);
    std::vector<std::string> values;
    string_utl::split_string(country_list, ',', values);

    for(int i=0;i<values.size();i++) {
        std::string ip_list = cJSON_GetObjectItem(pRoot, values[i].c_str())->valuestring;
        std::vector<std::string> values_ip;
        string_utl::split_string(ip_list, ',', values_ip);
        
        g_country_ip_list[values[i]] = values_ip;
    }
    std::map<std::string, std::vector<std::string>>::iterator it;
    for(it=g_country_ip_list.begin();it!= g_country_ip_list.end();it++)
        printf("iplist:%s,%d\n", it->first.c_str(), it->second.size());
    
    fflush ( stdout );
    cJSON_Delete(pRoot);
    return 0;
}
// select random ip from ip_list
int get_ip_list(const std::string& country, std::string& ip_list) {
    std::map<std::string, std::vector<std::string>>::iterator it;
    for (it = g_country_ip_list.begin(); it != g_country_ip_list.end(); it++) {
        if (it->first != country)
            continue;
        if (it->second.empty()) {
            ERROR("get ip_list fail:%s", country.c_str());
            break;
        }
        int index = time_utl::get_random(it->second.size());
        ip_list = it->second[index];
        INFO("return %s ip:%s,id:%d", it->first.c_str(), ip_list.c_str(), index);
        return 0;
    }
    ip_list = g_country_ip_list["US"][time_utl::get_random(g_country_ip_list["US"].size())];
    INFO("return default ip:%s", ip_list.c_str());
    return 0;
}
void display_row(MYSQL* con, MYSQL_ROW& sqlrow) {
   unsigned int field_count;

   field_count = 0;
   while (field_count < mysql_field_count(con)) {
      if (sqlrow[field_count]) printf("%s ", sqlrow[field_count]);
      else printf("NULL");
      field_count++;
   }
   printf("\n");
}
int check_user(MYSQL_ROW& sqlrow, const std::string& user_name,const std::string& user_password){
    
    return 0;
}
int  convert_to_MYSQL_TIME(time_t t, MYSQL_TIME& sqlTime)
   {
      struct tm timeinfo;

      timeinfo = *gmtime(&t);
      sqlTime.year = timeinfo.tm_year + 1900;
      sqlTime.month = timeinfo.tm_mon + 1;
      sqlTime.day = timeinfo.tm_mday;
      sqlTime.hour = timeinfo.tm_hour;
      sqlTime.minute = timeinfo.tm_min;
      sqlTime.second = timeinfo.tm_sec;

     return 0;
   }
/*MYSQL_DATE  convert_to_MYSQL_DATE(time_t t)
   {
      struct tm * timeinfo;
      MYSQL_DATE sqlDate;

      timeinfo = gmtime(&t);
      sqlDate.year = timeinfo.tm_year;
      sqlDate.month = timeinfo.tm_mon;
      sqlDate.day = timeinfo.tm_mday;

     return sqlDate;
   }
*/
int get_traffic(uint8_t premium, const std::string user_id, int& day_traffic, int& month_traffic) {
    MYSQL_STMT* stmt = mysql_stmt_init(g_mysql_con);
    char query[512];
    if (premium >=2) {
        strcpy(query, "select round(sum(in_traffic+out_traffic)/1024) as m,round(sum(CASE when op_date=curdate() then in_traffic+out_traffic else 0 end)/1024) as d from tinyvpn.user_traffic where email = ? and op_date >= date_add(curdate(),INTERVAL - DAY (curdate()) + 1 DAY);");
        if(mysql_stmt_prepare(stmt, query, strlen(query)))
        {
            fprintf(stderr, "mysql_stmt_prepare: %s\n", mysql_error(g_mysql_con));
            mysql_stmt_close(stmt);             
            return -1;
        }
    } else {
        strcpy(query, "select round(sum(in_traffic+out_traffic)/1024) as m,round(sum(CASE when op_date=curdate() then in_traffic+out_traffic else 0 end)/1024) as d from tinyvpn.user_traffic where device_id = ? and op_date >= date_add(curdate(),INTERVAL - DAY (curdate()) + 1 DAY);");
        if(mysql_stmt_prepare(stmt, query, strlen(query)))
        {
            fprintf(stderr, "mysql_stmt_prepare: %s\n", mysql_error(g_mysql_con));
            mysql_stmt_close(stmt);             
            return -1;
        }
    }
    MYSQL_BIND params[1];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = (void*)user_id.c_str();
    params[0].buffer_length = user_id.size();

    MYSQL_BIND results[2];
    memset(results, 0, sizeof(results));
    results[0].buffer_type = MYSQL_TYPE_LONG;
    results[0].buffer = &month_traffic;
    results[1].buffer_type = MYSQL_TYPE_LONG;
    results[1].buffer = &day_traffic;
    mysql_stmt_bind_param(stmt, params);
    mysql_stmt_bind_result(stmt, results); 
    mysql_stmt_execute(stmt); 
    mysql_stmt_store_result(stmt); 

    while(mysql_stmt_fetch(stmt) == 0) {
        INFO("return user traffic: %s,%d,%d", user_id.c_str(), day_traffic, month_traffic);
    }
    mysql_stmt_close(stmt);
    day_traffic = htonl(day_traffic);
    month_traffic = htonl(month_traffic);
    return 0;
}

int process_cmd(int fd, char* buff , int len) {
	// cmd 1byte
	// 0:get ip, input: 1byte premium,2 byte country, device_id or email, 
	//      return: 4byte day traffic, 4byte month traffic, 4byte day_limit, 4byte month_limit, protocol:ip:port
	// 1:register or login, input: device_id, '\n', email, '\n', password
	//      return 1byte login_ok, 1byte premium, 4byte day_traffic, 4byte month_traffic, 4byte day_limit, 4byte month_limit
	// 2:user charge, input: 1byte premium, email
	//      return 1byte login_ok, 1byte premium, 4byte day_traffic, 4byte month_traffic, 4byte day_limit, 4byte month_limit
	// 8: traffic data
	if (buff[0] == 0) {
    	mysql_ping(g_mysql_con);
		std::string ip_list;
        uint8_t premium = buff[1];
        buff += 2;
        len -= 2;
		std::string country(buff, 2);
		get_ip_list(country, ip_list);
        buff += 2;
        len -= 2;

        std::string user_id(buff, len);  // device_id or email
        int day_traffic =0;
        int month_traffic =0;
        if (get_traffic(premium, user_id, day_traffic, month_traffic) != 0)
            return -1;
        ip_list = std::string((char*)&day_traffic, 4) + std::string((char*)&month_traffic, 4) + 
            std::string((char*)&g_day_limit, 4) + std::string((char*)&g_month_limit, 4) + ip_list;  // day_traffic and month_traffic

		file_utl::write(fd, (char*)ip_list.c_str(), ip_list.size()); // send ip list
	} else if (buff[0] == 1) {
    	mysql_ping(g_mysql_con);
        MYSQL_STMT *stmt = mysql_stmt_init(g_mysql_con);
        char query[128];
        strcpy(query, "select id, password, premium from tinyvpn.user where email = ?;");
        if(mysql_stmt_prepare(stmt, query, strlen(query)))
        {
            fprintf(stderr, "mysql_stmt_prepare: %s\n", mysql_error(g_mysql_con));
            mysql_stmt_close(stmt);             
            return -1;
        }
//        INFO("prepare ok");
        MYSQL_BIND params[1];
        memset(params, 0, sizeof(params));
        std::string strInput(buff+1, len-1);
        std::vector<std::string> values;
        string_utl::split_string(strInput, '\n', values);
        if (values.size() < 3) {
            INFO("user email and password error.");
            mysql_stmt_close(stmt);             
            return 1;
        }
        std::string device_id = values[0];
        std::string email = values[1];
        std::string password = values[2];
        if (device_id.size() < 2 || email.size() < 2 || password.size() <1) {
            INFO("user email and password length error.%s,%s,%s", device_id.c_str(), email.c_str(), password.c_str());
            mysql_stmt_close(stmt);             
            return 1;
        }
        params[0].buffer_type = MYSQL_TYPE_STRING;
        params[0].buffer = (void*)email.c_str();
        params[0].buffer_length = email.size();
        INFO("recv user,device_id:%s, email:%s,password:%s", device_id.c_str(), email.c_str(), password.c_str());

        MYSQL_BIND results[3];
        memset(results, 0, sizeof(results));
        int id;
        char password2[32];
        int premium;
        results[0].buffer_type = MYSQL_TYPE_LONG;
        results[0].buffer = &id;
        results[1].buffer_type = MYSQL_TYPE_STRING;
        results[1].buffer = password2;
        results[1].buffer_length = sizeof(password2);
        results[2].buffer_type = MYSQL_TYPE_LONG;
        results[2].buffer = &premium;
        mysql_stmt_bind_param(stmt, params);
        mysql_stmt_bind_result(stmt, results); 
        mysql_stmt_execute(stmt); 
        mysql_stmt_store_result(stmt); 

        int get_user = 0;
        while(mysql_stmt_fetch(stmt) == 0) {
            INFO("get user: %d,%s,%s,%d", id, email.c_str(), password2, premium);
            get_user = 1;
        }
        mysql_stmt_close(stmt);             
        
        std::string strUserStatus;
        if(get_user == 0) {
            strUserStatus += (char)0;
            strUserStatus += (char)1;

            strcpy(query, "insert into tinyvpn.user(email, password, premium) values(?, ?, 1);");
            stmt = mysql_stmt_init(g_mysql_con);
            if(mysql_stmt_prepare(stmt, query, strlen(query)))
            {
                fprintf(stderr, "mysql_stmt_prepare: %s\n", mysql_error(g_mysql_con));
                mysql_stmt_close(stmt);             
                return -1;
            }            
            MYSQL_BIND paramsInsert[2];
            memset(paramsInsert, 0, sizeof(paramsInsert));
            paramsInsert[0].buffer_type = MYSQL_TYPE_STRING;
            paramsInsert[0].buffer = (void*)values[1].c_str();
            paramsInsert[0].buffer_length = values[1].size();
            paramsInsert[1].buffer_type = MYSQL_TYPE_STRING;
            paramsInsert[1].buffer = (void*)values[2].c_str();
            paramsInsert[1].buffer_length = values[2].size();
            mysql_stmt_bind_param(stmt, paramsInsert);
            if (mysql_stmt_execute(stmt))    {
              fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
              mysql_stmt_close(stmt);             
              return 1;
            }
            int affected_rows= mysql_stmt_affected_rows(stmt);
            if (affected_rows != 1) /* validate affected rows */
            {
              fprintf(stderr, " invalid affected rows by MySQL\n");
              mysql_stmt_close(stmt);             
              return 1;
            }         
            INFO("insert into tinyvpn.user ok, email:%s, password:%s", values[1].c_str(), values[2].c_str());
            mysql_stmt_close(stmt);             
            strUserStatus += std::string(8, (char)0);  // day_traffic and month_traffic
            strUserStatus += std::string((char*)&g_day_limit, 4);  
            strUserStatus += std::string((char*)&g_month_limit, 4);  
        } else {
            if (password.compare(password2) == 0) {  // password correct
                strUserStatus += (char)0;
                strUserStatus += (char)premium;

                int day_traffic=0;
                int month_traffic=0;
                if (premium == 1) {
                    if (get_traffic(premium, device_id, day_traffic, month_traffic) != 0)
                        return -1;
                } else if (premium >= 2) {
                    if (get_traffic(premium, email, day_traffic, month_traffic) != 0)
                        return -1;
                }
                strUserStatus += std::string((char*)&day_traffic, 4);  // day_traffic and month_traffic
                strUserStatus += std::string((char*)&month_traffic, 4);  // day_traffic and month_traffic
                strUserStatus += std::string((char*)&g_day_limit, 4);  
                strUserStatus += std::string((char*)&g_month_limit, 4);  
            } else {  // password error
                strUserStatus += (char)1;
                strUserStatus += (char)0;
                strUserStatus += std::string(16, (char)0);  // day_traffic and month_traffic
            }

        }
        INFO("return user info:%s", string_utl::HexEncode(strUserStatus).c_str());
        file_utl::write(fd, (char*)strUserStatus.c_str(), strUserStatus.size()); // 1 byte status (0 succ, 1 fail), 1byte premium (1 basic, 2 premium)
    }
    else if (buff[0] == 2) {
        mysql_ping(g_mysql_con);
        MYSQL_STMT *stmt = mysql_stmt_init(g_mysql_con);
        char query[128];
        strcpy(query, "select id, password, premium from tinyvpn.user where email = ?;");
        if(mysql_stmt_prepare(stmt, query, strlen(query)))
        {
            fprintf(stderr, "mysql_stmt_prepare: %s\n", mysql_error(g_mysql_con));
            mysql_stmt_close(stmt);             
            return -1;
        }

        MYSQL_BIND params[1];
        memset(params, 0, sizeof(params));
        int new_premium = buff[1];
        std::string email(buff+2, len-2);

        if ( email.size() < 2 ) {
            INFO("user email and password length error.%s", email.c_str());
            mysql_stmt_close(stmt);             
            return 1;
        }
        params[0].buffer_type = MYSQL_TYPE_STRING;
        params[0].buffer = (void*)email.c_str();
        params[0].buffer_length = email.size();
        INFO("recv user, email:%s", email.c_str());

        MYSQL_BIND results[3];
        memset(results, 0, sizeof(results));
        int id;
        char password2[32];
        int premium;
        results[0].buffer_type = MYSQL_TYPE_LONG;
        results[0].buffer = &id;
        results[1].buffer_type = MYSQL_TYPE_STRING;
        results[1].buffer = password2;
        results[1].buffer_length = sizeof(password2);
        results[2].buffer_type = MYSQL_TYPE_LONG;
        results[2].buffer = &premium;
        mysql_stmt_bind_param(stmt, params);
        mysql_stmt_bind_result(stmt, results); 
        mysql_stmt_execute(stmt); 
        mysql_stmt_store_result(stmt); 

        int get_user = 0;
        while(mysql_stmt_fetch(stmt) == 0) {
            INFO("get user: %d,%s,%s,%d", id, email.c_str(), password2, premium);
            get_user = 1;
        }
        mysql_stmt_close(stmt);             

        std::string strUserStatus;
        if(get_user == 0) {
            strUserStatus += (char)1;
            strUserStatus += (char)0;
            strUserStatus += std::string(16, (char)0);  // day_traffic and month_traffic
        } else {
// set as premium user
            strcpy(query, "update tinyvpn.user set premium = ? where email = ?;");
            stmt = mysql_stmt_init(g_mysql_con);
            if(mysql_stmt_prepare(stmt, query, strlen(query)))
            {
                fprintf(stderr, "mysql_stmt_prepare: %s\n", mysql_error(g_mysql_con));
                mysql_stmt_close(stmt);             
                return -1;
            }            
            MYSQL_BIND params[2];
            memset(params, 0, sizeof(params));
            params[0].buffer_type = MYSQL_TYPE_LONG;
            params[0].buffer = &new_premium;
            params[1].buffer_type = MYSQL_TYPE_STRING;
            params[1].buffer = (void*)email.c_str();
            params[1].buffer_length = email.size();
            mysql_stmt_bind_param(stmt, params);
            if (mysql_stmt_execute(stmt))    {
              fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
              mysql_stmt_close(stmt);             
              return 1;
            }
            int affected_rows= mysql_stmt_affected_rows(stmt);
            if (affected_rows != 1) /* validate affected rows */
            {
              fprintf(stderr, " invalid affected rows by MySQL\n");
              mysql_stmt_close(stmt);             
              return 1;
            }         
            INFO("update tinyvpn.user ok, email:%s, premium:%d", email.c_str(), new_premium);
            mysql_stmt_close(stmt);    
        
            strUserStatus += (char)0;
            strUserStatus += (char)new_premium;
    
            int day_traffic=0;
            int month_traffic=0;
            if (get_traffic(premium, email, day_traffic, month_traffic) != 0)
                return -1;
            strUserStatus += std::string((char*)&day_traffic, 4);  // day_traffic and month_traffic
            strUserStatus += std::string((char*)&month_traffic, 4);  // day_traffic and month_traffic
            strUserStatus += std::string((char*)&g_day_limit, 4);  
            strUserStatus += std::string((char*)&g_month_limit, 4);  
        
        }

        INFO("return user info:%s", string_utl::HexEncode(strUserStatus).c_str());
        file_utl::write(fd, (char*)strUserStatus.c_str(), strUserStatus.size()); // 1 byte status (0 succ, 1 fail), 1byte premium (1 basic, 2 premium)
        
	}else if (buff[0] == 8) {
    	mysql_ping(g_mysql_con);
        std::string strInput(buff+1, len-1);
	    DEBUG("traffic string:%s", strInput.c_str());
        std::vector<std::string> values;
        string_utl::split_string(strInput, '\n', values);
        if (values.empty()) {
            ERROR("no traffic data: %s", strInput.c_str());
            return 1;
        }
        MYSQL_STMT *stmt;
        MYSQL_TIME  ts1;
        MYSQL_TIME  ts2;
        char query[512];
        strcpy(query, "insert into tinyvpn.user_traffic(device_id, email, op_date, op_time, socket_duration,last_rtt,in_traffic,out_traffic,in_packets,out_packets) values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
        stmt = mysql_stmt_init(g_mysql_con);
        if(mysql_stmt_prepare(stmt, query, strlen(query)))
        {
            ERROR("mysql_stmt_prepare: %s\n", mysql_error(g_mysql_con));
            mysql_stmt_close(stmt);             
            return -1;
        }            
        int param_count= mysql_stmt_param_count(stmt);
        DEBUG("total parameters in INSERT: %d", param_count);
        for(int i=0;i<values.size();i++){
            std::vector<std::string> fields;
            string_utl::split_string(values[i], '\t', fields);
            if (fields.size() < 10) {
                ERROR("not enough fileds:%d",fields.size());
                continue;
            }
            INFO("insert data:%s,%s,%s,%s,%s,%s,%s,%s,%s,%s",fields[0].c_str(),fields[1].c_str(),fields[2].c_str(),fields[3].c_str(),
                fields[4].c_str(),fields[5].c_str(),fields[6].c_str(),fields[7].c_str(),fields[8].c_str(),fields[9].c_str());
            MYSQL_BIND paramsInsert[10];
            memset(paramsInsert, 0, sizeof(paramsInsert));
            char str_device_id[128];
            char str_name[128];
            char str_date[16];
            char str_date_time[32];
            unsigned long str_length[4];
            uint32_t socket_duration;
            uint32_t last_rtt;
            uint64_t in_traffic;
            uint64_t out_traffic;
            uint32_t in_packets;
            uint32_t out_packets;
            paramsInsert[0].buffer_type = MYSQL_TYPE_STRING;
            paramsInsert[0].buffer = (void*)str_device_id;
            paramsInsert[0].length = &str_length[0];
            paramsInsert[1].buffer_type = MYSQL_TYPE_STRING;
            paramsInsert[1].buffer = (void*)str_name;
            paramsInsert[1].length = &str_length[1];
            paramsInsert[2].buffer_type = MYSQL_TYPE_STRING;
            paramsInsert[2].buffer= (void*)str_date;
            paramsInsert[2].length= &str_length[2];
            paramsInsert[3].buffer_type = MYSQL_TYPE_STRING;
            paramsInsert[3].buffer= (void*)str_date_time;
            paramsInsert[3].length= &str_length[3];
            paramsInsert[4].buffer_type = MYSQL_TYPE_LONG;
            paramsInsert[4].buffer = &socket_duration;
            paramsInsert[5].buffer_type = MYSQL_TYPE_SHORT;
            paramsInsert[5].buffer = &last_rtt;
            paramsInsert[6].buffer_type = MYSQL_TYPE_LONGLONG;
            paramsInsert[6].buffer = &in_traffic;
            paramsInsert[7].buffer_type = MYSQL_TYPE_LONGLONG;
            paramsInsert[7].buffer = &out_traffic;
            paramsInsert[8].buffer_type = MYSQL_TYPE_LONG;
            paramsInsert[8].buffer = &in_packets;
            paramsInsert[9].buffer_type = MYSQL_TYPE_LONG;
            paramsInsert[9].buffer = &out_packets;

            if (mysql_stmt_bind_param(stmt, paramsInsert)) {
                ERROR("mysql_stmt_bind_param error: %s\n", mysql_stmt_error(stmt));
                mysql_stmt_close(stmt);
                return 1;
            }
            strcpy(str_device_id, fields[0].c_str());
            strcpy(str_name, fields[1].c_str());
            strcpy(str_date, fields[2].c_str());
            strcpy(str_date_time, fields[3].c_str());
            str_length[0] = strlen(str_device_id);
            str_length[1] = strlen(str_name);
            str_length[2] = strlen(str_date);
            str_length[3] = strlen(str_date_time);
            socket_duration = std::stoul(fields[4]);
            last_rtt = std::stoul(fields[5]);
            in_traffic = std::stoull(fields[6]);
            out_traffic = std::stoull(fields[7]);
            in_packets = std::stoul(fields[8]);
            out_packets = std::stoul(fields[9]);
            
            if (mysql_stmt_execute(stmt))    {
                ERROR("execute error: %s\n", mysql_stmt_error(stmt));
                mysql_stmt_close(stmt);
                return 1;
            }
            int affected_rows= mysql_stmt_affected_rows(stmt);
            if (affected_rows != 1) /* validate affected rows */
            {
                ERROR(" invalid affected rows by MySQL\n");
                mysql_stmt_close(stmt);
                return 1;
            }
            INFO("insert into tinyvpn.user_traffic ok");
        }
        mysql_stmt_close(stmt);
	} else {
	    std::string strtemp(buff+1, len-1);
	    INFO("unknown cmd id:%s", string_utl::HexEncode(strtemp).c_str());
	}
	return 0;
}
int main(int argc, const char * argv[]) {
    const int BUF_SIZE = 4096;
    srand(time(0));

	char buff[BUF_SIZE];
    OpenFile("./wlog.txt");

    g_mysql_con = mysql_init(NULL);
    if (g_mysql_con == NULL) {
        fprintf(stderr, "%s\n", mysql_error(g_mysql_con));
        exit(EXIT_FAILURE);
    }
    char value = 1;
    mysql_options(g_mysql_con, MYSQL_OPT_RECONNECT, (char *)&value);

    if (!mysql_real_connect(g_mysql_con, "localhost", "root", "123456", NULL, 0, NULL, 0)) {
        fprintf(stderr, "%s\n", mysql_error(g_mysql_con));
         mysql_close(g_mysql_con);
         exit(EXIT_FAILURE);
    }  
/*    int res = mysql_query(con, "show databases;");
    if (res) {
         fprintf(stderr, "SELECT error: %s\n", mysql_error(con));
         mysql_close(con);
         exit(EXIT_FAILURE);
    } else {
         res_ptr = mysql_use_result(con);
         if (res_ptr) {
            while ((sqlrow = mysql_fetch_row(res_ptr))) {
               display_row(con, sqlrow);
            }
            if (mysql_errno(con)) {
                fprintf(stderr, "Retrive error: %s\n", mysql_error(con));
            }
            mysql_free_result(res_ptr);
         }
    }
    printf("mariadb is connect and run succesed!\n");*/

    // read config
    std::ifstream is("config.json");
    is.read(buff, sizeof(buff));
    if (is.gcount() == 0) {
        INFO("read config error");
        return 1;
    }
    INFO("config,%d:%s", is.gcount(), buff);
    is.close();

    if (json_parse_config(buff) != 0) {
        INFO("json parse error.");
        return 0;
    }

    int sock_fd;
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr,"Socket error:%s\n\a", strerror(errno));
        exit(1);
    }
    const int flag = 1; //resuse -address and port as default
    int rc = ::setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag,sizeof (int));
    
	struct sockaddr_in server_addr;
    // ���sockaddr_in�ṹ
    bzero(&server_addr, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(60315);
    
    // ��sock_fd������
    if (bind(sock_fd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr)) == -1) {
        fprintf(stderr,"Bind error:%s\n\a", strerror(errno));
        exit(1);
    }
    INFO("bind ok.");
    
    // ����sock_fd������
    if(listen(sock_fd, 5) == -1) {
        fprintf(stderr,"Listen error:%s\n\a", strerror(errno));
        exit(1);
    }
    INFO("listen ok.");

    const uint16_t SELECT_NUMBER = 64;
    fd_set fdsr;
    fd_set listen_fds;
    int ret;
    int fd_A[SELECT_NUMBER];
    int maxfd = 0;
    memset(fd_A, 0, SELECT_NUMBER*sizeof(int));

    while(1) {
        FD_ZERO(&fdsr);
        FD_SET(sock_fd, &fdsr);
        maxfd = sock_fd;
        // ������������ӵ��ļ���������
        for (int i = 0; i < SELECT_NUMBER; i++) {
            if (fd_A[i] != 0) {
                FD_SET(fd_A[i], &fdsr);
                maxfd = std::max(maxfd, fd_A[i]);
            }
        }
        // ��ȡ�ļ����������л�Ծ�����ӣ�û�н�����ֱ����ʱ
        int nReady = select(maxfd + 1, &fdsr, NULL, NULL, NULL);
        if (nReady < 0) {
            INFO("select error");
            break;
        } else if (nReady == 0) {
            INFO("select timeout");
            continue;
        }
        if (FD_ISSET(sock_fd, &fdsr))  {
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(client_addr);
            int conn_fd = accept(sock_fd, (struct sockaddr*)&client_addr,&len);
            if (conn_fd <= 0) {
                INFO("accept error");
                continue;
            }
            int accept_ok = 0;
            for(int j = 0; j < SELECT_NUMBER; j++){
                if(fd_A[j]!=0)
                    continue;
                fd_A[j] = conn_fd;
                accept_ok = 1;
                INFO("accept ok, id:%d, fd:%d", j, conn_fd);
                break;
            }
            if (accept_ok == 0) {
                INFO("accept error, fd is full.");
                break;
            }
            if (--nReady == 0)  // read over
                continue;
        
        }
        for (int i = 0; i < SELECT_NUMBER; i++) {
            if (FD_ISSET(fd_A[i], &fdsr)) {
                DEBUG("check fd%d", i);
                ret = file_utl::read(fd_A[i], buff, BUF_SIZE);
                if (ret <= 0) {
                    // �ͻ��˹ر�
                    close(fd_A[i]);
                    FD_CLR(fd_A[i], &fdsr);
                    INFO("client closed,id:%d,fd:%d", i, fd_A[i]);
                    fd_A[i] = 0;
                } else {
//                    DEBUG("recv from socket, size:%d,%s", ret, string_utl::HexEncode(std::string(buff, ret)).c_str());
                    DEBUG("recv from socket, size:%d", ret);
					process_cmd(fd_A[i], buff, ret);

                    close(fd_A[i]);
                    FD_CLR(fd_A[i], &fdsr);
                    INFO("server closed,id:%d,fd:%d", i, fd_A[i]);
                    fd_A[i] = 0;
                }
                if (--nReady == 0)  // read over
                    break;
            }
        }
        
    }
    printf("server quit.\n");
    fflush ( stdout );


    mysql_close(g_mysql_con);   

    return 0;
}
