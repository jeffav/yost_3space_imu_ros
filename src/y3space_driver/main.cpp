#include <Y3SpaceDriver.h>
#include <thread>
#include <memory>
#include <signal.h>

bool stop_signal = false;
int frequency;
std::string NODE_NAME_ = "Y3SpaceDriver";
std::shared_ptr<std::thread> imu_thread;
std::shared_ptr<Y3SpaceDriver> imu_device;
ros::Publisher imu_publisher;


int getFrequency()
{
	static int cnt = 1;
	static ros::Time ref_time = ros::Time::now();
	cnt++;

	if(ros::Time::now().toSec() - ref_time.toSec() >= 1.0)
	{
		ref_time = ros::Time::now();
		int result = cnt;
		cnt = 0;
		return result;
	}

	return -1;
}



//!
//! IMU Thread Method
//!
void imu_thread_function(long timeout)
{
	while(!stop_signal)
	{
		//Here You can process and publish IMU data with smart pointer imu_device
		sensor_msgs::Imu imu;
		if (!imu_device->getImuMessage(imu))
			imu_publisher.publish(imu);
		usleep(1);
	}
}

//!
//! Signal Handler Method
//!
void signal_handler(int signal)
{
	stop_signal = true;
	ros::shutdown();
	exit(0);
}

int main(int argc, char **argv)
{
	signal(SIGINT, signal_handler);
  	ros::init(argc, argv, NODE_NAME_, ros::init_options::NoSigintHandler);
  	ros::NodeHandle nh;
  	ros::NodeHandle pnh("~");

  	imu_publisher = nh.advertise<sensor_msgs::Imu>("/y3space/imu", 400);

  	std::string port;
  	int baudrate;
  	int timeout;
  	std::string mode;
  	std::string frame;

    pnh.param<std::string>("port", port, "/dev/ttyACM0");
    pnh.param<int>("baudrate", baudrate, 115200);
    pnh.param<int>("timeout", timeout, 60000);
    pnh.param<std::string>("mode", mode, "relative");
    pnh.param<std::string>("frame", frame, "imu_link");
    pnh.param<int>("frequency", frequency, 1);

    //Create a non ros thread
    long time_out = 1000000/frequency;
    //ROS_INFO("Thread TIMEOUT: %ld", time_out);
    imu_device = std::make_shared<Y3SpaceDriver>(nh, pnh, port, baudrate, timeout, mode, frame);

    imu_thread = std::make_shared<std::thread>(&imu_thread_function, time_out);
    imu_thread->join();

  	//Y3SpaceDriver driver(nh, pnh, port, baudrate, timeout, mode, frame);
  	//driver.run();


  	//ros::spin();
}
