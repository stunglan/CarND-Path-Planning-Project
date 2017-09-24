# CarND-Path-Planning-Project
Hand-in in the Self-Driving Car Engineer Nanodegree Program
   

### Goals given by Udacity
In this project your goal is to safely navigate around a virtual highway with other traffic that is driving +-10 MPH of the 50 MPH speed limit. You will be provided the car's localization and sensor fusion data, there is also a sparse map list of waypoints around the highway. The car should try to go as close as possible to the 50 MPH speed limit, which means passing slower traffic when possible, note that other cars will try to change lanes too. The car should avoid hitting other cars at all cost as well as driving inside of the marked road lanes at all times, unless going from one lane to another. The car should be able to make one complete loop around the 6946m highway. Since the car is trying to go 50 MPH, it should take a little over 5 minutes to complete 1 loop. Also the car should not experience total acceleration over 10 m/s^2 and jerk that is greater than 50 m/s^3.



## Details

## Rubric 

### Compilation
The code compiles and run ok. I have used xcode on a mac to run it. The excecutable file is a level down, so the path to the data file is adjusted in line 422 to ```string map_file_ = "../../data/highway_map.csv";```, this may vary dependent of cmake target architecture.

### Valid Trajectories
The code should satisfy these points.

### Reflection

I have used the strategies provided by Aaron and David in the project walhthrough, and hence the part that they cover is refering to their comments during the walkthough and answers to questions.

#### Path Generation
I have used the approach described by Aaron.

In the function ```calculateTrajectory``` I calculate the path by using the previous previous path, adding calculated path points provided by the ```spline``` helper function to get to 3 positions further ahead.

In the special case where I have no previous path, I use the angle of the car to create one.

Jerks along the driving directions (```s```) are avoided by only gradually increase or decrease the speed up to a maximum speed. The can will only decrease speed until it stops, and ny reverse.

Jerks laterar to the driving direction (```d```) are  avoided using the ```spline``` helper functions with a sufficien long target distance. 

The path generated also accept the lane as a parameter and will generate lane shifting paths if the lane changes.


#### Changing lane state behavior

No full state machine is implemented, only two states exists: *continue straight ahead* and *I want to change lane*. The default mode is to continue straight ahead in the same lane, but if the car closes in on a car (in the function ```tooClose```), the mode is shifted and the code will try to find another lane.

The best lane is then determinated in in the function ```findBestLane```. This function uses a cost function (implemented in ```laneCost```). The cost function have only two contributing cost generators: ```penalizeShift``` and ```safeLaneShift```. The ```penalizeShift``` function will only slightly penalize to shift lane, in order to avoid situation driving between lanes where the car does not see the difference between tow different lanes. The ```safeLaneShift``` function will first penalize slightly to stay in the current line, penalize heavily if we want to change outside the legal lanes, and then also heavy if the projected path is either into a lane and a slot where there is a car projected to be in the slot in a distance of the car, or there is a car coming up behind.

The strategy is a conservative one, in that the car will not change lanes unless the margins are safe. And also the behaviour does not try to change path to a optimal lane, if the lane two lanes over do not have any traffic.



