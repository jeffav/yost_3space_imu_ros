#include <Y3SpaceDriver.h>


const std::string Y3SpaceDriver::logger = "[ Y3SpaceDriver ] ";
const std::string Y3SpaceDriver::MODE_ABSOLUTE = "absolute";
const std::string Y3SpaceDriver::MODE_RELATIVE = "relative";

Y3SpaceDriver::Y3SpaceDriver(ros::NodeHandle& nh, ros::NodeHandle& pnh, const std::string &port,
		int baudrate, int timeout, const std::string &mode, const std::string &frame):
    SerialInterface(port, baudrate, timeout),
    m_pnh(pnh),
    m_nh(nh),
    m_mode(mode),
    m_frame(frame),
	debug_(false)
{
	getParams();

    this->serialConnect();

    initDevice();

    ROS_INFO_STREAM(this->logger << "Ready\n");
    //this->m_imuPub = this->m_nh.advertise<sensor_msgs::Imu>("/mavros/imu/data", 10);
    //this->m_tempPub = this->m_nh.advertise<std_msgs::Float64>("/imu/temp", 10);
    //this->m_rpyPub = this->m_nh.advertise<geometry_msgs::Vector3Stamped>("/imu/rpy", 10);
}

sensor_msgs::Imu &Y3SpaceDriver::getImuMessage()
{
	//We assume the device is initialized
	//Getting untared orientation as Quaternion
	this->serialWriteString(GET_UNTARED_ORIENTATION_AS_QUATERNION_WITH_HEADER);
	std::string quaternion_msg = this->serialReadLine();
	std::vector<double>quaternion_arr = parseString<double>(quaternion_msg);	

	this->serialWriteString(GET_CORRECTED_GYRO_RATE);
	std::string gyro_msg = this->serialReadLine();
	std::vector<double>gyro_arr = parseString<double>(gyro_msg);


	this->serialWriteString(GET_CORRECTED_ACCELEROMETER_VECTOR);
	std::string accel_msg = this->serialReadLine();
	std::vector<double>accel_arr = parseString<double>(accel_msg);

	// Prepare IMU message
	ros::Time sensor_time = getReadingTime(quaternion_arr[1]);
	imu_msg_.header.stamp           = sensor_time;

	imu_msg_.header.frame_id        = "body_FLU";
	imu_msg_.orientation.x          = quaternion_arr[2];
	imu_msg_.orientation.y          = quaternion_arr[3];
	imu_msg_.orientation.z          = quaternion_arr[4];
	imu_msg_.orientation.w          = quaternion_arr[5];
	
	imu_msg_.angular_velocity.x     = gyro_arr[0];
	imu_msg_.angular_velocity.y     = gyro_arr[1];
	imu_msg_.angular_velocity.z     = gyro_arr[2];

	imu_msg_.linear_acceleration.x  = 9.8*accel_arr[0];
	imu_msg_.linear_acceleration.y  = 9.8*accel_arr[1];
	imu_msg_.linear_acceleration.z  = 9.8*accel_arr[2];

	return imu_msg_;
}

void Y3SpaceDriver::getParams()
{
	m_pnh.param<int>("frequency", imu_frequency_, 400);
	m_pnh.param<bool>("debug", debug_, false);
	m_pnh.param<bool>("magnetometer_enabled", magnetometer_enabled_, true);
	m_pnh.param<double>("timestamp_offset", timestamp_offset_, 0.012);
}

ros::Time Y3SpaceDriver::getYostRosTime(long sensor_time)
{
	ros::Time result;
	result.nsec = sensor_time % 1000000;
	result.sec = sensor_time / 1000000;

	return result;
}

Y3SpaceDriver::~Y3SpaceDriver()
{
	//resetSensor();

	serialDisConnect();
}

void Y3SpaceDriver::restoreFactorySettings()
{
    this->serialWriteString(RESTORE_FACTORY_SETTINGS);
}

const std::string Y3SpaceDriver::getSoftwareVersion()
{
    this->serialWriteString(GET_FIRMWARE_VERSION_STRING);

    const std::string buf = this->serialReadLine();
    ROS_INFO_STREAM(this->logger << "Software version: " << buf);
    return buf;
}

const std::string Y3SpaceDriver::getAxisDirection()
{
    this->serialWriteString(GET_AXIS_DIRECTION);

    const std::string buf = this->serialReadLine();
    const std::string ret = [&]()
    {
        if(buf == "0\r\n")
        {
            return "X: Right, Y: Up, Z: Forward";
        }
        else if ( buf == "1\r\n")
        {
            return "X: Right, Y: Forward, Z: Up";
        }
        else if ( buf == "2\r\n")
        {
            return "X: Up, Y: Right, Z: Forward";
        }
        else if (buf == "3\r\n")
        {
            return "X: Forward, Y: Right, Z: Up";
        }
        else if( buf == "4\r\n")
        {
            return "X: Up, Y: Forward, Z: Right";
        }
        else if( buf == "5\r\n")
        {
            return "X: Forward, Y: Up, Z: Right";
        }
        else if (buf == "19\r\n")
        {
            return "X: Forward, Y: Left, Z: Up";
        }
        else
        {
            ROS_WARN_STREAM(this->logger << "Buffer indicates: " + buf);
            return "Unknown";
        }
    }();

    ROS_INFO_STREAM(this->logger << "Axis Direction: " << ret);
    return ret;
}

void Y3SpaceDriver::startGyroCalibration(void)
{
    ROS_INFO_STREAM(this->logger << "Starting Auto Gyro Calibration...");
    this->serialWriteString(BEGIN_GYRO_AUTO_CALIB);
  
    ros::Duration(5.0).sleep();
    ROS_INFO_STREAM(this->logger << "Proceeding");
}

void Y3SpaceDriver::setMIMode(bool on)
{
    if(on)
    {
        this->serialWriteString(SET_MI_MODE_ENABLED);
    }
    else
    {
        this->serialWriteString(SET_MI_MODE_DISABLED);
    }
}

void Y3SpaceDriver::setMagnetometer(bool on)
{
    if(on)
    {
        this->serialWriteString(SET_MAGNETOMETER_ENABLED);
    }
    else
    {
        this->serialWriteString(SET_MAGNETOMETER_DISABLED);
    }
}

/*
 * **********************************************
 * Device Setup
 * ..............................................
 */
void Y3SpaceDriver::initDevice()
{
	//this->startGyroCalibration();
	this->getSoftwareVersion();
	this->setAxisDirection();
	this->setHeader();
	this->setMagnetometer(magnetometer_enabled_);
	this->setFilterMode();
	this->serialWriteString(SET_REFERENCE_VECTOR_CONTINUOUS);

	this->resetTimeStamp();

	sleep(2);

	this->serialWriteString(GET_FILTER_MODE);
	ROS_INFO("GET_FILTER_MODE: %s", this->serialReadLine().c_str());
	// this->setFrequency();
	// this->setStreamingSlots();
	this->getAxisDirection();
	this->getCalibMode();
	this->getMIMode();
	this->getMagnetometerEnabled();
	this->flushSerial();

	this->syncTimeStamp();
}

void Y3SpaceDriver::resetTimeStamp()
{
	this->serialWriteString(UPDATE_CURRENT_TIMESTAMP);
	ros_time_start_ = ros::Time::now();
	ROS_INFO_STREAM("RESETTING SENSOR TIME STAMP at " << ros_time_start_);
}

void Y3SpaceDriver::syncTimeStamp()
{
	std::vector<double> latency;
	std::vector<double> sensor_time;
	std::string sensor_msg;
	std::vector<double>sensor_msg_arr;
	ros::Time sensor_time_ros;

	// Zero Yost time
	this->resetTimeStamp();

	// Calculate message latency
	for (int i = 0; i < 5; ++i)
	{
		this->serialWriteString(GET_UNTARED_ORIENTATION_AS_QUATERNION_WITH_HEADER);
		sensor_msg = this->serialReadLine();
		sensor_msg_arr = parseString<double>(sensor_msg);

		sensor_time.push_back( sensor_msg_arr[1] / 1000000 );

		latency.push_back ((ros::Time::now().toSec() - ros_time_start_.toSec() - sensor_time.back()) /2 );
	}

	double average = std::accumulate( latency.begin(), latency.end(), 0.0) / latency.size();
	ROS_INFO_STREAM(this->logger << "Average Yost IMU message latency is " << average);

	msg_latency_ = average;
}

void Y3SpaceDriver::setFilterMode()
{
	this->serialWriteString(SET_FILTER_MODE_KALMAN);
}

const std::string Y3SpaceDriver::getCalibMode()
{
    this->serialWriteString(GET_CALIB_MODE);

    const std::string buf = this->serialReadLine();
    const std::string ret = [&]()
    {
        if(buf == "0\r\n")
        {
            return "Bias";
        }
        else if ( buf == "1\r\n")
        {
            return "Scale and Bias";
        }
        else
        {
            ROS_WARN_STREAM(this->logger << "Buffer indicates: " + buf);
            return "Unknown";
        }
    }();

    ROS_INFO_STREAM(this->logger << "Calibration Mode: " << ret);
    return ret;
}

const std::string Y3SpaceDriver::getMIMode()
{
    this->serialWriteString(GET_MI_MODE_ENABLED);

    const std::string buf = this->serialReadLine();
    const std::string ret = [&]()
    {
        if(buf == "0\r\n")
        {
            return "Disabled";
        }
        else if ( buf == "1\r\n")
        {
            return "Enabled";
        }
        else
        {
            ROS_WARN_STREAM(this->logger << "Buffer indicates: " + buf);
            return "Unknown";
        }
    }();

    ROS_INFO_STREAM(this->logger << "MI Mode: " << ret);
    return ret;
}

const std::string Y3SpaceDriver::getMagnetometerEnabled()
{
    this->serialWriteString(GET_MAGNETOMETER_ENABLED);

    const std::string buf = this->serialReadLine();
    const std::string ret = [&]()
    {
        if(buf == "0\r\n")
        {
            return "Disabled";
        }
        else if ( buf == "1\r\n")
        {
            return "Enabled";
        }
        else
        {
            ROS_WARN_STREAM(this->logger << "Buffer indicates: " + buf);
            return "Unknown";
        }
    }();

    ROS_INFO_STREAM(this->logger << "Magnetometer enabled state: " << ret);
    return ret;
}


std::string Y3SpaceDriver::getFrequencyMsg(int frequency)
{
	float period = 1000000.0/(float)frequency;
	std::string msg = ":82,"+std::to_string((int)period)+",0,0\n";
	std::cout << msg << std::endl;
	return msg;
}

void Y3SpaceDriver::setFrequency()
{
	this->serialWriteString(getFrequencyMsg(imu_frequency_).c_str());
}

void Y3SpaceDriver::setHeader()
{
	// Ask for timestamp and success byte
	this->serialWriteString(SET_HEADER_TS_SUCCESS);
	// Returns no data
}

void Y3SpaceDriver::setStreamingSlots()
{
	//Set Streaming Slot
	this->serialWriteString(SET_STREAMING_SLOTS_AUTOMODALITY);
	this->serialWriteString(GET_STREAMING_SLOTS);
	ROS_INFO("Y3SpaceDriver: GET_STREAMING_SLOTS POST CONFIGURATION: %s", this->serialReadLine().c_str());
}

void Y3SpaceDriver::setAxisDirection()
{
	//AXIS DIRECTION
	this->serialWriteString(SET_AXIS_DIRECTIONS_FLU);
}

//! Run the serial sync
void Y3SpaceDriver::run()
{
	bool devices_are_synched = false;
    std::vector<double> parsedVals;
    sensor_msgs::Imu imuMsg;
    geometry_msgs::Vector3Stamped imuRPY;
    std_msgs::Float64 tempMsg;

    initDevice();


    this->serialWriteString(START_STREAMING);

    ROS_INFO_STREAM(this->logger << "Ready\n");

    //Complete HACK to make sure that the buffer is empty
    for(int k = 0; k < 10; k++)
    {
    	this->serialReadLine();
    }


    ros::Rate rate(500);
    int line = -2;
    int expected_lines_ = 6;
    while(ros::ok())
    {
        while(this->available() > 0)
        {
            std::string buf = this->serialReadLine();
            //if(line == 0)
            //{
            //    ROS_INFO("BUFFER[%d]: %s",line, buf.c_str());
                //ros::Time now_t = ros::Time::now();
                //std::cout << "NOW TIME: " << now_t.nsec << std::endl;

                /*struct timeval start;
                gettimeofday(&start, NULL);
                long long milliseconds = (start.tv_sec*1000000LL + start.tv_usec) % 1000000000;
                std::cout << "NOW TIME: " << milliseconds << std::endl;*/


            //}

            std::string parse;
            std::stringstream ss(buf);
            std::stringstream ss_tmp(buf);
            double i;
            //Wait for the beginning of the message
            if(line < 0)
            {
            	// Parse data from the line
            	while (ss_tmp >> i)
            	{
            		//time stamp is a large number which is at the beginning of the message
            		if(i / 1000.0 > 10)
            		{
            			//We found the time stamp
            			if(line == -1)
            			{
                			ROS_INFO("found the start: %f", i);
            			}
            			line++;
            			break;
            		}
            		if (ss_tmp.peek() == ',')
            			ss_tmp.ignore();
            	}
            }
            if(line > -1)
            {
            	line += 1;
            	// Parse data from the line
            	while (ss >> i)
            	{
            		parsedVals.push_back(i);
            		if (ss.peek() == ',')
            			ss.ignore();
            	}

            	// Should stop reading when line == number of tracked streams
            	if(line == expected_lines_)
            	{

            		int j = 0;
            		for_each(parsedVals.begin(), parsedVals.end(), [&](double x){if(x / 1000.0 > 10) {j++;}});
            		if(j > 1)
            		{
            			ROS_INFO("something went wrong. there are more than one timestamp in here. recalculating the offset...");
            			if(debug_)
            			{
            				ROS_INFO("parsedVals size: %d", (int)parsedVals.size());
            				int cnt = 0;
            				for_each(parsedVals.begin(), parsedVals.end(), [&](double x){ROS_INFO("Value[%d] = %f", cnt++, x);});
            				ROS_INFO("---------------------------------");
            			}
            			parsedVals.clear();
            			line=-1;
            			continue;
            		}


            		if(debug_)
            		{
            			ROS_INFO("parsedVals size: %d", (int)parsedVals.size());
            			int cnt = 0;
            			for_each(parsedVals.begin(), parsedVals.end(), [&](double x){ROS_INFO("Value[%d] = %f", cnt++, x);});
            			ROS_INFO("---------------------------------");
            		}


            		//Perform synchronization when the data is properly received
            		if(!devices_are_synched)
            		{
            			reference_time_.first = ros::Time::now();
            			reference_time_.second = toRosTime(parsedVals[0]);
            			devices_are_synched = true;
            		}

            		// Reset line tracker
            		line = 0;

            		/* for 6,38,39,41,44
            		 *	Array Structure:
            		 *	idx 0 --> timestamp
            		 *  idx 1-4 --> untared orientation as Quaternion 6
            		 * 	idx 5-7 --> untared orientation as Euler Angles 7
            		 * 	idx 8-10 --> corrected gyroscope vector 38
            		 * 	idx 11-13 --> corrected accelerometer vector 39
            		 * 	idx 14-16 --> corrected compass vector 40
            		 * 	idx 17-19 --> corrected linear acceleration 41
            		 */



            		// Prepare IMU message
            		ros::Time sensor_time = getReadingTime(parsedVals[0]);
            		imuMsg.header.stamp           = sensor_time;
            		//ROS_INFO("Time Difference: %f", ros::Time::now().toSec() - sensor_time.toSec());
            		imuMsg.header.frame_id        = "body_FLU";

            		imuMsg.orientation.x          = parsedVals[1];
            		imuMsg.orientation.y          = parsedVals[2];
            		imuMsg.orientation.z          = parsedVals[3];
            		imuMsg.orientation.w          = parsedVals[4];

            		imuMsg.angular_velocity.x     = parsedVals[8];
            		imuMsg.angular_velocity.y     = parsedVals[9];
            		imuMsg.angular_velocity.z     = parsedVals[10];

            		imuMsg.linear_acceleration.x  = 9.8*parsedVals[11];
            		imuMsg.linear_acceleration.y  = 9.8*parsedVals[12];
            		imuMsg.linear_acceleration.z  = 9.8*parsedVals[13];

            		// Prepare temperature messages
					tempMsg.data = parsedVals[14];

            		// Clear parsed values
					parsedVals.clear();

            		/*imuRPY.vector = getRPY(imuMsg.orientation);

            		imuRPY.vector.x = getDegree(imuRPY.vector.x);
            		imuRPY.vector.y = getDegree(imuRPY.vector.y);
            		imuRPY.vector.z = getDegree(imuRPY.vector.z);*/

					//imuRPY.vector.x = getDegree((double)parsedVals[5]);
					//imuRPY.vector.y = getDegree((double)parsedVals[6]);
					//imuRPY.vector.z = getDegree((double)parsedVals[7]);
					//geometry_msgs::Quaternion q = getQuaternion((double)parsedVals[5],(double)parsedVals[6],(double)parsedVals[7]);
					//std::cout << q << std::endl;

					imuMsg.orientation = toENU(imuMsg.orientation);

            		this->m_imuPub.publish(imuMsg);
            		//this->m_tempPub.publish(tempMsg);
            		//this->m_rpyPub.publish(imuRPY);


            	}
            }

        }

        // Throttle ROS at fixed Rate
        rate.sleep();
        ros::spinOnce();
    }
}

ros::Time Y3SpaceDriver::toRosTime(double sensor_time)
{
	ros::Time res;
	res.sec = (long)sensor_time / 1000000;
	res.nsec = ((long) sensor_time % 1000000) * 1000;

	return res;
}

/* Returns Yost sensor time converted to ROS time
*  @param sensor_time internal clock time of sensor in microseconds
*/
ros::Time Y3SpaceDriver::getReadingTime(double sensor_time)
{
	ros::Duration ros_sensor_time = ros::Duration(0, sensor_time*1000);

	if (ros_sensor_time.sec > 3)
		syncTimeStamp();

	// Add in 2x msg_latency to account for two messages -- initial sync message and current message
	ros::Time result = ros_time_start_ + ros_sensor_time + ros::Duration(msg_latency_ * 2);

	if (debug_) {
		ROS_INFO_STREAM_THROTTLE(1,"Yost|ROS timestamp " << result);
		ros::Time now = ros::Time::now();
		ROS_INFO_THROTTLE(1,"\tros_time_now: %f\n\t\tRaw Sensor Time: %f, result: %f, msg latency: %f\n\t\tage of data: %f sec",
				now.toSec(), ros_sensor_time.toSec(), result.toSec(), msg_latency_, now.toSec()-result.toSec());
	}
	
	return result;
}

geometry_msgs::Vector3 Y3SpaceDriver::getRPY(geometry_msgs::Quaternion &q)
{
	geometry_msgs::Vector3 vect;

	/*tf2::Quaternion tf_q;
	tf2::convert(q,tf_q);
	tf2::Matrix3x3 m(tf_q);
	m.getRPY(vect.x, vect.y, vect.z);
	return vect;*/

	tf2::Quaternion tfq(q.x, q.y, q.z, q.w);
	tf2::Matrix3x3 m(tfq);
	m.getRPY(vect.x, vect.y, vect.z);
	return vect;
}

geometry_msgs::Quaternion Y3SpaceDriver::getQuaternion(double roll, double pitch, double yaw)
{
	tf::Quaternion q;
	q.setRPY(roll,pitch,yaw);
	geometry_msgs::Quaternion q_msg;
	quaternionTFToMsg(q, q_msg);
	return q_msg;
}

geometry_msgs::Quaternion Y3SpaceDriver::toENU(geometry_msgs::Quaternion q)
{
	tf::Quaternion q_FLU(q.x, q.y, q.z, q.w);
	tf::Quaternion q_TF(0.0, 0.0, 0.707, 0.707);
	tf::Quaternion q_ENU = q_TF * q_FLU;

	geometry_msgs::Quaternion q_MSG;
	quaternionTFToMsg(q_ENU, q_MSG);

	return q_MSG;
}

double Y3SpaceDriver::getDegree(double rad)
{
	return rad * 180.0 / M_PI;
}

