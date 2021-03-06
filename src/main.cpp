#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}



// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;
  
  //cout << "maps_s[prev_wp+1] " << maps_s[prev_wp+1] << std::endl;;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}



// calculate trajectory and return nextvals
// walktrougth code using spline
// create a widely spaced (x,y) waypoints, evenly spaced at 30 m
void calculateTrajectory(vector<double> &next_x_vals,vector<double> &next_y_vals,double car_s,double car_d,double car_x,double car_y,double car_yaw,int prev_size,vector<double>  previous_path_x, vector<double>  previous_path_y, vector<double> &map_waypoints_x,vector<double> &map_waypoints_y,vector<double> &map_waypoints_s, vector<double>&map_waypoints_dx,vector<double> &map_waypoints_dy,int lane,double ref_vel, double projecting_dist ) {
  
  vector<double> ptsx;
  vector<double> ptsy;
  
  // reference x,y and yaw
  double ref_x = car_x;
  double ref_y = car_y;
  double ref_yaw = deg2rad(car_yaw);
  
  // if previous size is almost empty , use the car as starting reference
  if(prev_size < 2)
  {
    // use two point that make the path target to the car
    double prev_car_x = car_x - cos(car_yaw);
    double prev_car_y = car_y - sin(car_yaw);
    
    ptsx.push_back(prev_car_x);
    ptsx.push_back(car_x);
    ptsy.push_back(prev_car_y);
    ptsy.push_back(car_y);
    
  }
  else
  {
    // redefine reference point to the last path
    ref_x = previous_path_x[prev_size-1];
    ref_y = previous_path_y[prev_size-1];
    
    double ref_x_prev = previous_path_x[prev_size-2];
    double ref_y_prev = previous_path_y[prev_size-2];
    
    ref_yaw = atan2(ref_y-ref_y_prev, ref_x - ref_x_prev);
    
    // use two points that make a path tangent to the previous point's end point
    ptsx.push_back(ref_x_prev);
    ptsx.push_back(ref_x);
    
    ptsy.push_back(ref_y_prev);
    ptsy.push_back(ref_y);
    
  }
  
  vector<double> next_mp0 = getXY(car_s + 30, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
  vector<double> next_mp1 = getXY(car_s + 60, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
  vector<double> next_mp2 = getXY(car_s + 90, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
  
  ptsx.push_back(next_mp0[0]);
  ptsx.push_back(next_mp1[0]);
  ptsx.push_back(next_mp2[0]);
  
  ptsy.push_back(next_mp0[1]);
  ptsy.push_back(next_mp1[1]);
  ptsy.push_back(next_mp2[1]);
  
  // changes the cars angle to 0 degrees
  for (int i = 0; i < ptsx.size();i++)
  {
    double shift_x = ptsx[i]-ref_x;
    double shift_y = ptsy[i]-ref_y;
    
    ptsx[i] = (shift_x * cos(0-ref_yaw) - shift_y*sin(0-ref_yaw));
    ptsy[i] = (shift_x * sin(0-ref_yaw) + shift_y*cos(0-ref_yaw));
    
  }
  
  tk::spline s;
  
  s.set_points(ptsx, ptsy);
  
  // start with all the previous path points from last time
  for (int i = 0; i < previous_path_x.size();i++)
  {
    next_x_vals.push_back(previous_path_x[i]);
    next_y_vals.push_back(previous_path_y[i]);
    
  }
  
  // calculate how to break up spline so that we travel at out desired reference velocity
  double target_x = projecting_dist;
  double target_y = s(target_x);
  double target_dist = sqrt(target_x*target_x + target_y*target_y);
  
  double x_add_on = 0;
  
  // fill up the rest of our path planner after filling it with previous points, here we will always output 50 points
  for (int i = 1; i <= (50-previous_path_x.size());i++)
  {
    double N = (target_dist/(0.02*ref_vel/2.24));
    double x_point = x_add_on+target_x/N;
    double y_point = s(x_point);
    
    x_add_on = x_point;
    
    double x_ref = x_point;
    double y_ref = y_point;
    
    // rotate back to normal
    x_point = x_ref*cos(ref_yaw)-y_ref*sin(ref_yaw);
    y_point = x_ref*sin(ref_yaw)+y_ref*cos(ref_yaw);
    
    x_point += ref_x;
    y_point += ref_y;
    
    next_x_vals.push_back(x_point);
    next_y_vals.push_back(y_point);
    
  }
}

// function to check if car before is to close
bool tooClose(int lane, double car_s, int prev_size, vector<vector<double>> &sensor_fusion)
{
  bool too_close = false;
  // find ref_v to use
  for (int i = 0; i < sensor_fusion.size(); i++) {
    // car is in my lane
    float d = sensor_fusion[i][6];
    if (d < (2+4*lane+2) && d > (2+4*lane-2))
    {
      double vx = sensor_fusion[i][3];
      double vy = sensor_fusion[i][4];
      double check_speed = sqrt(vx*vx+vy*vy);
      double check_car_s = sensor_fusion[i][5];
      
      check_car_s += ((double)prev_size*0.02*check_speed);  // if we are using previous points we can project the s value
      
      // check s values are greater than mine and s gap
      if((check_car_s > car_s) && ((check_car_s-car_s)< 30)) {
        //ref_vel = 29.5;
        too_close = true;
        
      }
    }
  }
  return too_close;
}


// penalize lane shift to not shift if same cost
double penalizeShift(int target_lane, int current_lane){
  double cost = 0;
  if (target_lane != current_lane)
    cost = 1.0;
  return cost;
  
}

// calculate the cost shifting to a lane
double safeLaneShift(int target_lane, int current_lane,double car_s, double car_speed, int prev_size, vector<vector<double>> &sensor_fusion,double projecting_dist)
{
  double cost = 0;
  
  // current lane is always safe
  if (target_lane == current_lane)
  {
    cost = 10;
    //cout << "checking if safe in current lane " << std::endl;
    return cost;
  }
  
  
  // unsafe to shift outside lanes
  if (target_lane < 0 || target_lane > 2) {
    cost = 9999.0;
    return cost;
  }

  for (int i = 0; i < sensor_fusion.size(); i++) {
    // car is in this lane
    float d = sensor_fusion[i][6];
    if (d < (2+4*target_lane+2) && d > (2+4*target_lane-2))
    {
      double vx = sensor_fusion[i][3];
      double vy = sensor_fusion[i][4];
      double check_speed = sqrt(vx*vx+vy*vy);
      double check_car_s = sensor_fusion[i][5];
      
      check_car_s += ((double)prev_size*0.02*check_speed);  // if we are using previous points we can project the s value
      
      // check s values are greater than mine and in the  gap
      if((check_car_s > car_s) && ((check_car_s-car_s)< projecting_dist)) {
        cout << "NOT SAFE in lane: " << target_lane << " car in front win the gap, distance: " << check_car_s-car_s  << std::endl;
        cost = 999.0; // not safe
        
      }
      // check if there is a car behind and greater speed
      else if ((check_car_s < car_s) && ((car_s-check_car_s)< projecting_dist)) {
        
        cout << "car in lane: " << target_lane << " behind, distance: " << car_s-check_car_s << " speed: " << check_speed << std::endl;
        cost = 10.0; // not safe
        
        // if larger speed not safe
        if (check_speed > car_speed)
        {
          cout << "      NOT SAFE  in lane: " << target_lane << " car behind with more speed, distance: " << car_s-check_car_s << " speed: " << check_speed << std::endl;
          cost = 999.0; // not safe
        }
      }

    }
  }
  return cost;
}



// calculate the cost for driving in a lane
double laneCost(int target_lane, int current_lane, double car_s, double car_speed, int prev_size, vector<vector<double>> &sensor_fusion,double projecting_dist)
{
  double cost = 0;
  
  cost += safeLaneShift(target_lane, current_lane,car_s,car_speed, prev_size, sensor_fusion,projecting_dist);
  cost += penalizeShift(target_lane, current_lane);

  cout << "cost lane: " << target_lane << " cost: " << cost  << std::endl;
  return cost;
}


// calculate the best lane and if we need to change
int findBestLane(int lane, double car_s, double car_speed, int prev_size, vector<vector<double>> &sensor_fusion,double projecting_dist)
{
  int nextLane = lane;

  double cost_left = laneCost(lane-1,lane, car_s,car_speed, prev_size, sensor_fusion,projecting_dist);
  double cost_straigth = laneCost(lane,lane, car_s,car_speed, prev_size, sensor_fusion,projecting_dist);
  double cost_rigth = laneCost(lane+1,lane, car_s,car_speed, prev_size, sensor_fusion,projecting_dist);
  
  if ((cost_rigth < cost_straigth) && (cost_rigth < cost_left)) nextLane = lane + 1;
  else if ((cost_left < cost_straigth)) nextLane = lane - 1;
  
  
  
  return nextLane;
}



int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  //string map_file_ = "../../data/highway_map.csv";  // changed to cope with Xcode build structure
  string map_file_ = "../data/highway_map.csv";  // changed to cope with Xcode build structure
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }
  
  // variables from walktrough
  int lane = 1; // target lane
  double ref_vel = 0; // target velocity mph
  double projecting_dist = 30; // smoot target distance for changinbg lane
  
  

  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy,&lane,&ref_vel,&projecting_dist](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          
          /*
           #### The map of the highway is in data/highway_map.txt
           Each waypoint in the list contains  [x,y,s,dx,dy] values. x and y are the waypoint's map coordinate position, the s value is the distance along the road to get to that waypoint in meters, the dx and dy values define the unit normal vector pointing outward of the highway loop.
           The highway's waypoints loop around so the frenet s value, distance along the road, goes from 0 to 6945.554.
           Here is the data provided from the Simulator to the C++ Program
           #### Main car's localization Data (No Noise)
           ["x"] The car's x position in map coordinates
           ["y"] The car's y position in map coordinate
           ["s"] The car's s position in frenet coordinates
           ["d"] The car's d position in frenet coordinates
           ["yaw"] The car's yaw angle in the map
           ["speed"] The car's speed in MPH
           #### Previous path data given to the Planner
           //Note: Return the previous list but with processed points removed, can be a nice tool to show how far along
           the path has processed since last time.
           ["previous_path_x"] The previous list of x points previously given to the simulator
           ["previous_path_y"] The previous list of y points previously given to the simulator
           #### Previous path's end s and d values
           ["end_path_s"] The previous list's last point's frenet s value
           ["end_path_d"] The previous list's last point's frenet d value
           #### Sensor Fusion Data, a list of all other car's attributes on the same side of the road. (No Noise)
           ["sensor_fusion"] A 2d vector of cars and then that car's [car's unique ID, car's x position in map coordinates, car's y position in map coordinates, car's x velocity in m/s, car's y velocity in m/s, car's s position in frenet coordinates, car's d position in frenet coordinates.
           */
          // j[1] is the data JSON object
        	// Main car's localization Data
            double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          vector<double> car;
          vector<vector<double>> sensor_fusion = j[1]["sensor_fusion"];
          
          //auto sensor_fusion = j[1]["sensor_fusion"];
          
          int prev_size = previous_path_x.size();

          	json msgJson;


            // TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          
          /*
           Acceleration is calculated by comparing the rate of change of average speed over .2 second intervals. In this case total acceleration at one point was as high as 75 m/s^2. Jerk was also very high. The jerk is calculated as the average acceleration over 1 second intervals. In order for the passenger to have an enjoyable ride both the jerk and the total acceleration should not exceed 10.
           */
          
          // use end of last projection
          if(prev_size > 0) {
            car_s = end_path_s;
          }
          
          
          // code for vehicle mitigation
          bool too_close = tooClose(lane, car_s, prev_size, sensor_fusion);
          
          if (too_close )  // only two states: wants to change lane, and not in need state
          {
            //
            cout << "wants to change from lane: " << lane << std::endl;
            lane = findBestLane(lane,car_s, car_speed, prev_size, sensor_fusion,projecting_dist);
          }
          
          // accellerate and decellerate depending on cars in front and when starting
          
          if (too_close)
          {
            ref_vel -= 0.224;
          }
          else if (ref_vel < 49.5)
          {
            ref_vel += 0.224;
          }
          

        
          
          // code for smooth lane following
          
          vector<double> next_x_vals;
          vector<double> next_y_vals;
   
          calculateTrajectory(next_x_vals,next_y_vals,car_s,car_d,car_x,car_y,car_yaw,
                              prev_size,previous_path_x,previous_path_y,
                              map_waypoints_x,map_waypoints_y,map_waypoints_s, map_waypoints_dx,map_waypoints_dy,
                              lane,ref_vel,projecting_dist );
          
          
          	// END // TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
