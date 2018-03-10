#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>

#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>

#include "ros/ros.h"

#include "tf/tf.h"
#include "tf2/LinearMath/Quaternion.h"

#include "sensor_msgs/NavSatFix.h"
#include "geometry_msgs/TwistStamped.h"
#include "geometry_msgs/Vector3Stamped.h"
#include "std_msgs/String.h"
#include "std_msgs/Int64.h"
#include "std_msgs/Int16.h"
#include "std_msgs/Float64.h"

#include "BathyBoatNav/robot_state.h"
#include "BathyBoatNav/message.h"
#include "BathyBoatNav/gps_conversion.h"
#include "BathyBoatNav/pid_coeff.h"
#include "../include/state.h"

#include <boost/algorithm/string.hpp>

using namespace std;

int socket_RV;
int socket_service;
struct sockaddr_in adr;
socklen_t lgadresse;

int gps_status;

string msg;

int isSending;
int port;

double x_lambert, y_lambert;
double latitude, longitude, yaw, pitch, roll, vit, wifi_lvl;
double u_yaw, u_throttle;

double batt1, batt2, batt3;

double left_mot, right_mot;

int state;

// SIGACTION
void signals_handler(int signal_number)
{
	printf("\nKilling server\n");
	close(socket_RV);
	close(socket_service);
	fflush(stdout);
	printf("\nClosed cleanly\n");
	exit(1);
}

void robotStateCallback(const BathyBoatNav::robot_state::ConstPtr& msg)
{
	state = msg->state;
	gps_status = msg->gps_status;

	latitude 	= msg->pose.position.x;
	longitude 	= msg->pose.position.y;

	tf::Quaternion q(msg->pose.orientation.x, msg->pose.orientation.y, msg->pose.orientation.z, msg->pose.orientation.w);
	tf::Matrix3x3 m(q);
	m.getRPY(roll, pitch, yaw);

	vit = msg->speed.linear.x;

	batt1 = msg->batteries.data[0];
	batt2 = msg->batteries.data[1];
	batt3 = msg->batteries.data[2];
}


void leftCallback(const std_msgs::Int64::ConstPtr& msg)
{
    left_mot = msg->data;
}

void rightCallback(const std_msgs::Int64::ConstPtr& msg)
{
    right_mot = msg->data;
}

void send_to ()
{
	ros::NodeHandle n;
	ros::Rate loop_rate(10);
	char buffer[500];

	ros::Subscriber left_sub 	= n.subscribe("left_mot", 1000, leftCallback);
	ros::Subscriber right_sub = n.subscribe("right_mot", 1000, rightCallback);
	ros::Subscriber vel_sub 	= n.subscribe("/robot_state_raw", 1000, robotStateCallback);

	while(ros::ok())
	{

		sprintf(buffer,"$POS;%lf;%lf;%lf;%lf;%lf;%d\n",ros::Time::now().toSec(), latitude, longitude, yaw, vit, gps_status);

		if( send(socket_service, buffer, strlen(buffer), 0) < 0 )
		{
			printf("End of connection\n");
			break;
		}

		sprintf(buffer,"$MOT;%lf;%lf;%lf\n",ros::Time::now().toSec(), left_mot, right_mot);
		
		if( send(socket_service, buffer, strlen(buffer), 0) < 0 )
		{
			printf("End of connection\n");
			break;
		}

		sprintf(buffer,"$BATT;%lf;%lf;%lf;%lf\n",ros::Time::now().toSec(), batt1, batt2, batt3);
		
		if( send(socket_service, buffer, strlen(buffer), 0) < 0 )
		{
			printf("End of connection\n");
			break;
		}

		sprintf(buffer,"$DATA;%lf;%lf;%lf\n",ros::Time::now().toSec(), 0.0, 0.0);
		
		if( send(socket_service, buffer, strlen(buffer), 0) < 0 )
		{
			printf("End of connection\n");
			break;
		}

		sprintf(buffer,"$STATE;%lf;%d\n",ros::Time::now().toSec(), state);
		
		if( send(socket_service, buffer, strlen(buffer), 0) < 0 )
		{
			printf("End of connection\n");
			break;
		}

		ros::spinOnce();
		loop_rate.sleep();
	}
}

void rec_from ()
{
    ros::NodeHandle n;
	ros::Rate loop_rate(10);
	char c[1];
	string rcv_msg;
	int res;
	int i, j;

	double speed_hat = 0.5;

	vector<string> split_msg;

    ros::Publisher speed_pub = n.advertise<std_msgs::Float64>("speed_hat", 1000);
    std_msgs::Float64 speed_msg;

	ros::ServiceClient mision_path_client = n.serviceClient<BathyBoatNav::message>("new_mission");
    BathyBoatNav::message mission_path_msg;

    ros::ServiceClient change_state_client = n.serviceClient<BathyBoatNav::message>("changeStateSrv");
    BathyBoatNav::message change_state_msg;

    ros::ServiceClient change_factor_client = n.serviceClient<BathyBoatNav::pid_coeff>("PID_coeff");
    BathyBoatNav::pid_coeff pid_coeff_msg;

	while(ros::ok())
	{
		do{
			res = recv(socket_service, c, 1, 0);

			if( res <= 0 )
			{
				printf("End of connection\n");
				break;
			}
			
			rcv_msg.append(c);
		} while((int)c[0] != 0);
		
		ROS_INFO("Command received : %s", rcv_msg.c_str());
		boost::split(split_msg, rcv_msg, boost::is_any_of("|"));
		
		if(split_msg[0] == "MISSION")
		{
			mission_path_msg.request.message = split_msg[1];

			if(mision_path_client.call(mission_path_msg))
			{                
				if(mission_path_msg.response.success)
				{
					ROS_INFO("Mission parsed");
				} else {
					ROS_INFO("Mission parsing failed");
				}
			} else {
				ROS_WARN("Call to mission interpreter failed");
			}

		} else if(split_msg[0] == "FACTOR") {

			pid_coeff_msg.request.k_P = atof(split_msg[1].c_str());
			pid_coeff_msg.request.k_I = atof(split_msg[2].c_str());

			if(change_factor_client.call(pid_coeff_msg))
			{                
				if(!pid_coeff_msg.response.success)
				{
					ROS_INFO("Mission parsing failed");
				}
			} else {
				ROS_WARN("Call to regul failed for changing k_I");
			}

		} else if(split_msg[0] == "SPEED") {
			speed_hat = atof(split_msg[1].c_str());
		} else {

			change_state_msg.request.message = split_msg[0];
			
			if(change_state_client.call(change_state_msg))
			{                
				if(!change_state_msg.response.success)
				{
					ROS_WARN("Failed to change state. Msg was %s", split_msg[1].c_str());
				}
			} else {
				ROS_WARN("Call to fsm failed");
			}
		}

		speed_msg.data = speed_hat;
		speed_pub.publish(speed_msg);

		rcv_msg.clear();
	}
}

void accept_loop()
{
	while(ros::ok())
	{

		printf("Waiting for connection...\n");
		socket_service = accept(socket_RV,(struct sockaddr *)&adr, &lgadresse);
		printf("Connection successful\n");

		if (socket_service < 0)
		{
	        perror("Accept error");
        	exit(1);
		}

		if(isSending == 1)
		{
			send_to();
		} else {
			rec_from();
		}
		
	}
}

void server(int port)
{
	if((socket_RV=socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("Unable to create socket");
		exit(1);
	}

	adr.sin_family 		= AF_INET;
	adr.sin_port 		= htons(port);
	adr.sin_addr.s_addr = htonl(INADDR_ANY);

	int opt = 1;
	setsockopt(socket_RV,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(int));

	if (bind(socket_RV, (struct sockaddr *) &adr, sizeof(adr))==-1)
	{
		perror("No bind");
		exit(1);
	}
	
	if (listen(socket_RV,1)==-1) // Ecoute si quelqu'un se connecte
	{
		perror("Unable to listen");
		exit(1);
	}
	
	accept_loop();
}


	// Main

int main(int argc, char *argv [])
{
	// SIGACTION
    struct sigaction action;
    action.sa_handler = signals_handler;
    sigemptyset(& (action.sa_mask));
    sigaction(SIGKILL, 	& action, NULL);
    sigaction(SIGTERM, 	& action, NULL);
    sigaction(SIGQUIT, 	& action, NULL);
    sigaction(SIGINT, 	& action, NULL);
    // END SIGACTION

	ros::init(argc, argv, "tcp_serveur");
    ros::NodeHandle n;
    
    //n.param<bool>("isSending", isSending, true);
    //n.param<int>("port", port, 29200);

    isSending = atoi(argv[1]);

    if(isSending == 1)
    {
    	n.getParam("/tcp/dataPort", port);
    } else {
		n.getParam("/tcp/commandPort", port);
    }

    yaw = 0.0;
    state = 0;

	printf("Starting server\n");

	server(port);

	printf("Killing server\n");
	close(socket_RV);
	close(socket_service);

	return EXIT_SUCCESS;
}
