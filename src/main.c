#include <stdio.h>
#include <stdbool.h>

int main(){

int level_percent;
bool pump_on = false;
bool sensor_fault = false;
int levels[5]={20, 20, 101, 101, 80};
int i; 

for(i=0; i<5; i++){

     level_percent = levels[i];

    printf("Level percent = %d\n", level_percent);

    bool previous_pump_on = pump_on;
    bool previous_sensor_fault = sensor_fault;


if(level_percent<=30 && level_percent>=0){
        pump_on = true;
        sensor_fault = false;  
        printf("pump is on\n");
    }
else if ( level_percent>30 && level_percent<80){
        sensor_fault = false; 
        printf("pump state is unchanged\n");
    }
else if (level_percent>=80 && level_percent<=100){
        pump_on = false;
        sensor_fault = false; 
        printf("pump is off\n");
    }
else {
        pump_on = false;
        sensor_fault = true;   
        printf("invalid range\n");
    }

    if((previous_pump_on==false)&&(pump_on==true)){
         printf("Notification: pump ON command sent\n");

    }
    if((previous_pump_on==true)&&(pump_on==false)){
            printf("Notification: pump OFF command sent\n");
         }
    if((previous_sensor_fault==false)&&(sensor_fault==true)){
            printf("Notification: sensor fault detected\n");
         }
        


    printf("pump state is : %d\n", pump_on);
    printf("sensor fault is : %d\n", sensor_fault);

}

return 0;

}