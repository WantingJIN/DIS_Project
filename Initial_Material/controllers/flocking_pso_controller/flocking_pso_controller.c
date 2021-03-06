/*****************************************************************************/
/* File:         raynolds2.c                                                 */
/* Version:      2.0                                                         */
/* Date:         06-Oct-15                                                   */
/* Description:  Reynolds flocking with relative positions		     */
/*                                                                           */
/* Author: 	 06-Oct-15 by Ali Marjovi				     */
/* Last revision:12-Oct-15 by Florian Maushart				     */
/*****************************************************************************/

#include <stdio.h>
#include <math.h>
#include <string.h>

#include <webots/robot.h>
/*Webots 2018b*/
#include <webots/motor.h>
/*Webots 2018b*/
#include <webots/differential_wheels.h>
#include <webots/distance_sensor.h>
#include <webots/emitter.h>
#include <webots/receiver.h>

#define NB_SENSORS 8  // Number of distance sensors
#define MIN_SENS 350  // Minimum sensibility value
#define MAX_SENS 4096 // Maximum sensibility value
#define MAX_SPEED 800 // Maximum speed
/*Webots 2018b*/
#define MAX_SPEED_WEB 6.28 // Maximum speed webots
/*Webots 2018b*/
#define FLOCK_SIZE 5 // Size of flock
#define TIME_STEP 64 // [ms] Length of time step

#define AXLE_LENGTH 0.052		// Distance between wheels of robot (meters)
#define SPEED_UNIT_RADS 0.00628 // Conversion factor from speed unit to radian per second
#define WHEEL_RADIUS 0.0205		// Wheel radius (meters)
#define DELTA_T 0.064			// Timestep (seconds)

//#define RULE1_THRESHOLD 0.20	// Threshold to activate aggregation rule. d//efault 0.20
//#define RULE1_WEIGHT (0.6 / 10) // Weight of aggregation rule. default 0.6/10

//#define RULE2_THRESHOLD 0.15	 // Threshold to activate dispersion rule. default 0.15
//#define RULE2_WEIGHT (0.02 / 10) // Weight of dispersion rule. default 0.02/10

//#define RULE3_WEIGHT (1.0 / 10) // Weight of consistency rule. default 1.0/10

//#define MIGRATION_WEIGHT (0.1 / 10) // Wheight of attraction towards the common goal. default 0.01/10

#define MIGRATORY_URGE 1 // Tells the robots if they should just go forward or move towards a specific migratory direction

#define ABS(x) ((x >= 0) ? (x) : -(x))

#define DATASIZE 5

/*Webots 2018b*/
WbDeviceTag left_motor;	 //handler for left wheel of the robot
WbDeviceTag right_motor; //handler for the right wheel of the robot
/*Webots 2018b*/

int e_puck_matrix[16] = {17, 29, 34, 10, 8, -38, -56, -76, -72, -58, -36, 8, 10, 36, 28, 18}; // for obstacle avoidance

WbDeviceTag ds[NB_SENSORS];	   // Handle for the infrared distance sensors
WbDeviceTag receiver_infrared; // Handle for the receiver node
WbDeviceTag emitter_infrared;  // Handle for the emitter node
WbDeviceTag receiver_radio;	   // Handle for the emitter node
WbDeviceTag receiver_loc;

int robot_id_u, robot_id; // Unique and normalized (between 0 and FLOCK_SIZE-1) robot ID

float relative_pos[FLOCK_SIZE][3];		// relative X, Z, Theta of all robots
float prev_relative_pos[FLOCK_SIZE][3]; // Previous relative  X, Z, Theta values
float my_position[3];					// X, Z, Theta of the current robot
float prev_my_position[3];				// X, Z, Theta of the current robot in the previous time step
float speed[FLOCK_SIZE][2];				// Speeds calculated with Reynold's rules
float relative_speed[FLOCK_SIZE][2];	// Speeds calculated with Reynold's rules
int initialized[FLOCK_SIZE];			// != 0 if initial positions have been received
float migr[2] = {3, 0};					// Migration vector
char *robot_name;
float initial_position[3];
int msl, msr; // Wheel speeds
float theta_robots[FLOCK_SIZE];

// Define the threshold and the weighting
float rule1_weight = 0.06;
float rule2_thres = 0.15;
float rule2_weight = 0.002;
float rule3_weight = 0.1;
float migration_weight = 0.01;

int loop_num = 1000;

/*
 * Reset the robot's devices and get its ID
 */
static void reset()
{
	wb_robot_init();
	receiver_infrared = wb_robot_get_device("receiver_infrared");
	receiver_radio = wb_robot_get_device("receiver_radio");
	emitter_infrared = wb_robot_get_device("emitter_infrared");
	receiver_loc = wb_robot_get_device("receiver_loc");

	//get motors
	left_motor = wb_robot_get_device("left wheel motor");
	right_motor = wb_robot_get_device("right wheel motor");
	wb_motor_set_position(left_motor, INFINITY);
	wb_motor_set_position(right_motor, INFINITY);

	int i;
	char s[4] = "ps0";
	for (i = 0; i < NB_SENSORS; i++)
	{
		ds[i] = wb_robot_get_device(s); // the device name is specified in the world file
		s[2]++;							// increases the device number
	}
	robot_name = (char *)wb_robot_get_name();

	for (i = 0; i < NB_SENSORS; i++)
		wb_distance_sensor_enable(ds[i], 64);

	wb_receiver_enable(receiver_infrared, 1);
	wb_receiver_enable(receiver_radio, 1);
	wb_receiver_enable(receiver_loc, 5);

	//Reading the robot's name. Pay attention to name specification when adding robots to the simulation!
	sscanf(robot_name, "epuck%d", &robot_id_u); // read robot id from the robot's name
	robot_id = robot_id_u % FLOCK_SIZE;			// normalize between 0 and FLOCK_SIZE-1

	for (i = 0; i < FLOCK_SIZE; i++)
	{
		initialized[i] = 0; // Set initialization to 0 (= not yet initialized)
	}

	// hard-code the initial state of robot [0]: x [1]: y [2]:theta
	initial_position[0] = -2.9;
	initial_position[2] = 0;
	if (robot_id == 0)
		initial_position[1] = 0.0;
	if (robot_id == 1)
		initial_position[1] = 0.1;
	if (robot_id == 2)
		initial_position[1] = -0.1;
	if (robot_id == 3)
		initial_position[1] = 0.2;
	if (robot_id == 4)
		initial_position[1] = -0.2;

	for (i = 0; i < 3; i++)
	{
		my_position[i] = initial_position[i];
	}
	msl = 0;
	msr = 0;
	wb_receiver_disable(receiver_infrared);
	printf("Reset: robot %d\n", robot_id_u);
}

/*
 * Keep given int number within interval {-limit, limit}
 */
void limit(int *number, int limit)
{
	if (*number > limit)
		*number = limit;
	if (*number < -limit)
		*number = -limit;
}

/*
 * Updates robot position with wheel speeds
 */
void update_self_motion(int msl, int msr)
{
	float theta = my_position[2];

	// Compute deltas of the robot
	float dr = (float)msr * SPEED_UNIT_RADS * WHEEL_RADIUS * DELTA_T;
	float dl = (float)msl * SPEED_UNIT_RADS * WHEEL_RADIUS * DELTA_T;
	float du = (dr + dl) / 2.0;
	float dtheta = (dr - dl) / AXLE_LENGTH;

	// Compute deltas in the environment
	float dx = -du * cosf(theta);
	float dz = du * sinf(theta);

	// Update position
	my_position[0] += dx;
	my_position[1] += dz;
	my_position[2] += dtheta;

	// Keep orientation within 0, 2pi
	if (my_position[2] > 2 * M_PI)
		my_position[2] -= 2.0 * M_PI;
	if (my_position[2] < 0)
		my_position[2] += 2.0 * M_PI;
}

/*
 * Computes wheel speed given a certain X,Z speed
 */
void compute_wheel_speeds(int *msl, int *msr)
{
	// Compute wanted position from Reynold's speed and current location
	float x = speed[robot_id][0] * cosf(my_position[2]) + speed[robot_id][1] * sinf(my_position[2]);  // x in robot coordinates
	float z = -speed[robot_id][0] * sinf(my_position[2]) + speed[robot_id][1] * cosf(my_position[2]); // z in robot coordinates
	// if (robot_id == 0)
	// {
	// 	printf("x is %f, z is %f \n", x, z);
	// }
	float Ku = 0.2;						// Forward control coefficient
	float Kw = 0.5;						// Rotational control coefficient
	float range = sqrtf(x * x + z * z); // Distance to the wanted position
	float bearing = -atan2(x, z);		// Orientation of the wanted position
	//printf("For robot %d, range is %f, bearing is %f \n", robot_id, range, bearing);
	// Compute forward control
	float u = Ku * range * cosf(bearing);
	// Compute rotational control
	float w = Kw * bearing;
	// printf("range is %f, bearing is %f \n", range, bearing);
	//printf("For robot %d, u is %f, w is %f\n", robot_id, u, w);

	// Convert to wheel speeds!
	*msl = (u - AXLE_LENGTH * w / 2.0) * (1000.0 / WHEEL_RADIUS);
	*msr = (u + AXLE_LENGTH * w / 2.0) * (1000.0 / WHEEL_RADIUS);
	//printf("For robot %d, convert, msl %d, mlr %d\n", robot_id, *msl, *msr);
	limit(msl, MAX_SPEED);
	limit(msr, MAX_SPEED);
}

/*
 *  Update speed according to Reynold's rules
 */
void reynolds_rules()
{
	int i, j, k;					 // Loop counters
	float rel_avg_loc[2] = {0, 0};	 // Flock average positions
	float rel_avg_speed[2] = {0, 0}; // Flock average speeds
	float cohesion[2] = {0, 0};
	float dispersion[2] = {0, 0};
	float consistency[2] = {0, 0};

	/* Compute averages over the whole flock */
	for (i = 0; i < FLOCK_SIZE; i++)
	{
		if (i == robot_id)
			continue;
		for (j = 0; j < 2; j++)
		{
			rel_avg_speed[j] += relative_speed[i][j];
			rel_avg_loc[j] += relative_pos[i][j];
		}
		//printf("relative position of robot [%d] is: [1]%f, [2]%f \n", i, relative_pos[i][0], relative_pos[i][1]);
	}
	for (j = 0; j < 2; j++)
	{
		rel_avg_speed[j] /= FLOCK_SIZE - 1;
		rel_avg_loc[j] /= FLOCK_SIZE - 1;
	}

	/* Rule 1 - Aggregation/Cohesion: move towards the center of mass */

	for (j = 0; j < 2; j++)
	{
		cohesion[j] = rel_avg_loc[j];
	}

	/* Rule 2 - Dispersion/Separation: keep far enough from flockmates */
	for (k = 0; k < FLOCK_SIZE; k++)
	{
		if (k != robot_id)
		{ // Loop on flockmates only
			// If neighbor k is too close (Euclidean distance)
			if (pow(relative_pos[k][0], 2) + pow(relative_pos[k][1], 2) < rule2_thres)
			{
				for (j = 0; j < 2; j++)
				{
					dispersion[j] -= 1 / relative_pos[k][j]; // Relative distance to k
				}
			}
		}
	}

	/* Rule 3 - Consistency/Alignment: match the speeds of flockmates */
	for (j = 0; j < 2; j++)
	{
		//consistency[j] = rel_avg_speed[j];
		consistency[j] = 0;
	}

	//aggregation of all behaviors with relative influence determined by weights
	for (j = 0; j < 2; j++)
	{
		// if (robot_id == 4)
		// {
		// 	printf("cohesion is %f %f, dispersion is %f %f\n", cohesion[0], cohesion[1], dispersion[0], dispersion[1]);
		// }
		speed[robot_id][j] = cohesion[j] * rule1_weight;
		speed[robot_id][j] += dispersion[j] * rule2_weight;
		speed[robot_id][j] += consistency[j] * rule3_weight;
	}
	speed[robot_id][1] *= -1; //y axis of webots is inverted

	//move the robot according to some migration rule
	if (MIGRATORY_URGE == 0)
	{
		speed[robot_id][0] += 0.01 * cos(my_position[2] + M_PI / 2);
		speed[robot_id][1] += 0.01 * sin(my_position[2] + M_PI / 2);
	}
	else
	{
		speed[robot_id][0] += (migr[0] - my_position[0]) * migration_weight;
		//y axis of webots is inverted
		speed[robot_id][1] -= (migr[1] - my_position[1]) * migration_weight;
		// if (robot_id == 0)
		// 	printf("migration is %f, %f\n", migr[0] - my_position[0], migr[1] - my_position[1]);
		// if (robot_id == 0)
		// 	printf("my position is %f, %f, %f\n", my_position[0], my_position[1], my_position[2]);
	}
}

/*
 *  each robot sends a ping message, so the other robots can measure relative range and bearing to the sender.
 *  the message contains the robot's name
 *  the range and bearing will be measured directly out of message RSSI and direction
*/
void send_ping(void)
{
	char out[10];
	strcpy(out, robot_name); // in the ping message we send the name of the robot.
	wb_emitter_send(emitter_infrared, out, strlen(out) + 1);
}

void process_localization_messages(void)
{
	char *inbuffer;
	int count = 0;
	int rob_nb;
	float rob_x, rob_z, rob_theta;
	while (wb_receiver_get_queue_length(receiver_loc) > 0 && count < FLOCK_SIZE)
	{
		inbuffer = (char *)wb_receiver_get_data(receiver_loc);
		sscanf(inbuffer, "%d#%f#%f#%f", &rob_nb, &rob_x, &rob_z, &rob_theta);
		//printf("Recevied message is %s\n", inbuffer);
		if (rob_nb == robot_id)
		{
			my_position[0] = rob_x;
			my_position[1] = -rob_z;
			my_position[2] = rob_theta;
		}
		count++;
		wb_receiver_next_packet(receiver_loc);
	}
}

/*
 * processing all the received ping messages, and calculate range and bearing to the other robots
 * the range and bearing are measured directly out of message RSSI and direction
*/
void process_received_ping_messages(void)
{
	const double *message_direction;
	double message_rssi; // Received Signal Strength indicator
	double theta;
	double range;
	char *inbuffer; // Buffer for the receiver node
	int other_robot_id;
	while (wb_receiver_get_queue_length(receiver_infrared) > 0)
	{
		inbuffer = (char *)wb_receiver_get_data(receiver_infrared);
		message_direction = wb_receiver_get_emitter_direction(receiver_infrared);
		message_rssi = wb_receiver_get_signal_strength(receiver_infrared);
		double y = message_direction[2];
		double x = message_direction[0];

		theta = -atan2(y, x);
		theta = theta + my_position[2]; // find the relative theta;
		range = sqrt((1 / message_rssi));

		other_robot_id = (int)(inbuffer[5] - '0'); // since the name of the sender is in the received message. Note: this does not work for robots having id bigger than 9!
		//printf("robot_id is: %d, other_robot_id is: %d", robot_id, other_robot_id);
		//printf("message_direction is: [0]%f, [1]%f, [2]%f\n", message_direction[0], message_direction[1], message_direction[2]);
		// Get position update

		// if (robot_id == 0)
		// {
		// 	printf("Robot id is: %d, range is: %f\n", other_robot_id, range);
		// }
		prev_relative_pos[other_robot_id][0] = relative_pos[other_robot_id][0];
		prev_relative_pos[other_robot_id][1] = relative_pos[other_robot_id][1];

		relative_pos[other_robot_id][0] = range * cos(theta);		 // relative x pos
		relative_pos[other_robot_id][1] = -1.0 * range * sin(theta); // relative y pos

		// if (robot_id == 2)
		// {
		// printf("relative_pos of robot %d is %f, %f\n", other_robot_id, relative_pos[other_robot_id][0], relative_pos[other_robot_id][0]);
		// }

		//printf("Robot %s, from robot %d, x: %g, y: %g, theta %g, my theta %g\n",robot_name,other_robot_id,relative_pos[other_robot_id][0],relative_pos[other_robot_id][1],-atan2(y,x)*180.0/3.141592,my_position[2]*180.0/3.141592);

		relative_speed[other_robot_id][0] = relative_speed[other_robot_id][0] * 0.0 + 1.0 * (1 / DELTA_T) * (relative_pos[other_robot_id][0] - prev_relative_pos[other_robot_id][0]);
		relative_speed[other_robot_id][1] = relative_speed[other_robot_id][1] * 0.0 + 1.0 * (1 / DELTA_T) * (relative_pos[other_robot_id][1] - prev_relative_pos[other_robot_id][1]);

		wb_receiver_next_packet(receiver_infrared);
	}
}

void process_received_weightings_from_supervisor()
{
	double *rbuffer;
	msl = 0;
	msr = 0;
	wb_motor_set_position(left_motor, INFINITY);
	wb_motor_set_position(right_motor, INFINITY);
	if (wb_receiver_get_queue_length(receiver_radio) > 0)
	{
		rbuffer = (double *)wb_receiver_get_data(receiver_radio);
		printf("Robot id: %d, Received weightings from supervisor and reset the postion for the motor\n", robot_id);
		int i, j;
		for (i = 0; i < FLOCK_SIZE; i++)
		{
			for (j = 0; j < 3; j++)
			{
				prev_relative_pos[i][j] = 0.0;
				relative_pos[i][j] = 0.0;
			}
		}
		for (i = 0; i < 3; i++)
		{
			for (j = 0; j < 2; j++)
			{
				speed[i][j] = 0.0;
				relative_speed[i][j] = 0.0;
			}
			prev_my_position[i] = 0.0;
			my_position[i] = initial_position[i];
		}

		rule1_weight = rbuffer[0] / 10;
		rule2_weight = rbuffer[1] / 10;
		//rule3_weight = rbuffer[2] / 10;
		//migration_weight = rbuffer[2] / 100;
		rule2_thres = rbuffer[2];
		loop_num = rbuffer[3];
		printf("weight: rule1 %f, rule2 %f, rule3 %f, migration %f, rule2_thres %f\n", rule1_weight, rule2_weight, rule3_weight, migration_weight, rule2_thres);
		wb_receiver_next_packet(receiver_radio);
	}
}

// the main function
int main()
{
	// /*Webots 2018b*/
	float msl_w, msr_w;
	// /*Webots 2018b*/
	int bmsl, bmsr, sum_sensors; // Braitenberg parameters
	int i, t;					 // Loop counter
	int distances[NB_SENSORS];	 // Array for the distance sensor readings
	int max_sens;				 // Store highest sensor value

	reset(); // Resetting the robot

	for (;;)
	{
		while (wb_receiver_get_queue_length(receiver_radio) == 0)
		{
			wb_motor_set_velocity(left_motor, 0);
			wb_motor_set_velocity(right_motor, 0);
			wb_robot_step(TIME_STEP);
		}
		process_received_weightings_from_supervisor();
		wb_receiver_enable(receiver_infrared, 1);

		/* Braitenberg */
		for (t = 0; t < loop_num; t++)
		{
			bmsl = 0;
			bmsr = 0;
			sum_sensors = 0;
			max_sens = 0;
			//printf("Robot_id: %d, position is: %f, %f, %f\n", robot_id, my_position[0], my_position[1], my_position[2]);
			for (i = 0; i < NB_SENSORS; i++)
			{
				distances[i] = wb_distance_sensor_get_value(ds[i]);			  //Read sensor values
				sum_sensors += distances[i];								  // Add up sensor values
				max_sens = max_sens > distances[i] ? max_sens : distances[i]; // Check if new highest sensor value

				// Weighted sum of distance sensor values for Braitenburg vehicle
				bmsr += e_puck_matrix[i] * distances[i];
				bmsl += e_puck_matrix[i + NB_SENSORS] * distances[i];
			}

			// Adapt Braitenberg values (empirical tests)
			bmsl /= MIN_SENS;
			bmsr /= MIN_SENS;
			bmsl += 66;
			bmsr += 72;

			/* Send and get information */
			send_ping(); // sending a ping to other robot, so they can measure their distance to this robot

			/// Compute self position
			prev_my_position[0] = my_position[0];
			prev_my_position[1] = my_position[1];

			//update_self_motion(msl, msr);

			process_received_ping_messages();

			process_localization_messages();

			speed[robot_id][0] = (1 / DELTA_T) * (my_position[0] - prev_my_position[0]);
			speed[robot_id][1] = (1 / DELTA_T) * (my_position[1] - prev_my_position[1]);

			// Reynold's rules with all previous info (updates the speed[][] table)
			reynolds_rules();
			//printf("For robot:%d, speed 0 is: %f, speed 1 is: %f\n", robot_id, speed[robot_id][0], speed[robot_id][1]);
			// Compute wheels speed from reynold's speed
			compute_wheel_speeds(&msl, &msr);
			//printf("For robot:%d, after reynold rule msl is: %d, msr is: %d\n", robot_id, msl, msr);
			// Adapt speed instinct to distance sensor values
			if (sum_sensors > NB_SENSORS * MIN_SENS)
			{
				msl -= msl * max_sens / (2 * MAX_SENS);
				msr -= msr * max_sens / (2 * MAX_SENS);
			}
			//printf("distance msl is: %d, msr is: %d\n", msl, msr);
			// Add Braitenberg
			//printf("For robot: %d, before add braitenberg, msl is: %d, msr is: %d \n", robot_id, msl, msr);
			msl += bmsl;
			msr += bmsr;
			//printf("For robot: %d, Add Braitenberg msl is: %d, msr is: %d\n", robot_id, bmsl, bmsr);
			// Set speed
			msl_w = msl * MAX_SPEED_WEB / 1000;
			msr_w = msr * MAX_SPEED_WEB / 1000;
			wb_motor_set_velocity(left_motor, msl_w);
			wb_motor_set_velocity(right_motor, msr_w);
			//printf("msl_w is: %f, msr_w is: %f\n", msl_w, msr_w);

			// Continue one step
			wb_robot_step(TIME_STEP);
		}
	}
}