#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/xlog.h>


#include "kd_camera_hw.h"


#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_camera_feature.h"




/******************************************************************************
 * Debug configuration
 ******************************************************************************/
#define PFX "[kd_camera_hw]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
//#define PK_DBG_FUNC(fmt, arg...)    xlog_printk(ANDROID_LOG_INFO, PFX , fmt, ##arg)
#define PK_DBG_FUNC(fmt, arg...) printk(KERN_ERR PFX fmt, ##arg)


#define DEBUG_CAMERA_HW_K
#ifdef DEBUG_CAMERA_HW_K
#define PK_DBG PK_DBG_FUNC
//#define PK_ERR(fmt, arg...)         xlog_printk(ANDROID_LOG_ERR, PFX , fmt, ##arg)
#define PK_ERR(fmtt, arg...) printk(KERN_ERR PFX fmt, ##arg)

#define PK_XLOG_INFO(fmt, args...) \
	do {    \
		xlog_printk(ANDROID_LOG_INFO, PFX , fmt, ##arg); \
	} while(0)
#else
#define PK_DBG(a,...)
#define PK_ERR(a,...)
#define PK_XLOG_INFO(fmt, args...)
#endif


#define IDX_PS_MODE 1
#define IDX_PS_ON   2
#define IDX_PS_OFF  3


#define IDX_PS_CMRST 0
#define IDX_PS_CMPDN 4


#ifndef BOOL
typedef unsigned char BOOL;
#endif

extern void ISP_MCLK1_EN(BOOL En);
extern void ISP_MCLK2_EN(BOOL En);
extern void ISP_MCLK3_EN(BOOL En);


u32 pinSetIdx = 0;//default main sensor
u32 pinSet[3][8] = {
	//for main sensor
	{  CAMERA_CMRST_PIN,
		CAMERA_CMRST_PIN_M_GPIO,   /* mode */
		GPIO_OUT_ONE,              /* ON state */
		GPIO_OUT_ZERO,             /* OFF state */
		CAMERA_CMPDN_PIN,
		CAMERA_CMPDN_PIN_M_GPIO,
		GPIO_OUT_ONE,
		GPIO_OUT_ZERO,
	},
	//for sub sensor
	{  CAMERA_CMRST1_PIN,
		CAMERA_CMRST1_PIN_M_GPIO,
		GPIO_OUT_ONE,
		GPIO_OUT_ZERO,
		CAMERA_CMPDN1_PIN,
		CAMERA_CMPDN1_PIN_M_GPIO,
		GPIO_OUT_ONE,
		GPIO_OUT_ZERO,
	},
};


//lenovo.sw wangsx3 use LDO,power custlist is invalid.
PowerCust PowerCustList={
	{
		{GPIO_UNSUPPORTED,GPIO_MODE_GPIO,Vol_Low},   //for AVDD;
		{GPIO_UNSUPPORTED,GPIO_MODE_GPIO,Vol_Low},   //for DVDD;
		{GPIO_UNSUPPORTED,GPIO_MODE_GPIO,Vol_Low},   //for DOVDD;
		{GPIO_UNSUPPORTED,GPIO_MODE_GPIO,Vol_Low},   //for AFVDD;
		{GPIO_UNSUPPORTED,GPIO_MODE_GPIO,Vol_Low},   //for AFEN;
	}
};



PowerUp PowerOnList={
	{
	{SENSOR_DRVNAME_IMX219OFILM_MIPI_RAW,
		  {
			  {PDN,   Vol_Low,	1},
			  {AVDD,  Vol_2800, 1},
			  {DOVDD, Vol_1800, 1},
			  {DVDD,  Vol_1200, 1},
		  	  {SensorMCLK,Vol_High, 1},	
			  {AFVDD, Vol_2800, 1}, 
			  {PDN,   Vol_High, 10},
		  },
	 },
	{SENSOR_DRVNAME_HI551QTECH_MIPI_RAW,
		  { 
			  {PDN,   Vol_Low,	1},
			  {RST,   Vol_Low,	1},
			  {DOVDD, Vol_1800, 1},
			  {AVDD,  Vol_2800, 1},
			  {PDN,   Vol_High, 1},
			  //following spec to turn on MCLK after xshudown
			  {SensorMCLK,Vol_High, 15},
			  {RST,   Vol_High, 5},	  
		  },
	 },
        {SENSOR_DRVNAME_HI551QTECHV2_MIPI_RAW,
                  {
                          {PDN,   Vol_Low,      1},
                          {RST,   Vol_Low,      1},
                          {DOVDD, Vol_1800, 1},
                          {AVDD,  Vol_2800, 1},
                          {PDN,   Vol_High, 1},
                          //following spec to turn on MCLK after xshudown
                          {SensorMCLK,Vol_High, 15},
                          {RST,   Vol_High, 5},
                  },
         },

		//add new sensor before this line
		{NULL,},
	}
};


//!!!notice power down list will be operated in reverse sequence, but not the exact reverse of power on
PowerUp PowerDownList={
        {
        {SENSOR_DRVNAME_IMX219OFILM_MIPI_RAW,
                  {
                          {PDN,   Vol_Low,      1},
                          {SensorMCLK,Vol_High, 0},
                          {AVDD,  Vol_2800, 1},
                          {DOVDD, Vol_1800, 1},
                          {DVDD,  Vol_1200, 1},
                          {AFVDD, Vol_2800, 1},
                 	  {PDN,   Vol_High, 1},	 
                  },
         },
        {SENSOR_DRVNAME_HI551QTECH_MIPI_RAW,
                  {
                          {PDN,   Vol_Low,      1},
                          {RST,   Vol_Low,      1},
                          {DOVDD, Vol_1800, 1},
                          {AVDD,  Vol_2800, 1},
                          //{DVDD,  Vol_1200, 1},
                          {PDN,   Vol_High, 1},
                          //following spec to turn on MCLK after xshudown
                          {SensorMCLK,Vol_High, 1},
                          {RST,   Vol_High, 15},
                  },
         },
        {SENSOR_DRVNAME_HI551QTECHV2_MIPI_RAW,
                  {
                          {PDN,   Vol_Low,      1},
                          {RST,   Vol_Low,      1},
                          {DOVDD, Vol_1800, 1},
                          {AVDD,  Vol_2800, 1},
                          //{DVDD,  Vol_1200, 1},
                          {PDN,   Vol_High, 1},
                          //following spec to turn on MCLK after xshudown
                          {SensorMCLK,Vol_High, 1},
                          {RST,   Vol_High, 15},
                  },
         },

                //add new sensor before this line
                {NULL,},
        }
};




BOOL hwpoweron(PowerInformation pwInfo, char* mode_name)
{
	if(pwInfo.PowerType == AVDD)
	{
		
		if(PowerCustList.PowerCustInfo[0].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			PK_DBG("[CAMERA SENSOR] AVDD power on");
			if(TRUE != hwPowerOn(pwInfo.PowerType,pwInfo.Voltage,mode_name))
			{
				PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
				return FALSE;
			}
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[0].Gpio_Pin,PowerCustList.PowerCustInfo[0].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[0].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[0].Gpio_Pin,PowerCustList.PowerCustInfo[0].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}			
	}
	else if(pwInfo.PowerType == DVDD)
	{
		if(PowerCustList.PowerCustInfo[1].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			if(pinSetIdx == 1)
			{
				PK_DBG("[CAMERA SENSOR] Sub camera VCAM_D power on");
				if(TRUE != hwPowerOn(SUB_CAMERA_POWER_VCAM_D,pwInfo.Voltage,mode_name))
				{
					PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
					return FALSE;
				}
			}	
			else
			{
				PK_DBG("[CAMERA SENSOR] Main camera VAM_D power on");
				if(TRUE != hwPowerOn(pwInfo.PowerType,pwInfo.Voltage,mode_name))
				{
					PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
					return FALSE;
				}
			}
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[1].Gpio_Pin,PowerCustList.PowerCustInfo[1].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[1].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[1].Gpio_Pin,PowerCustList.PowerCustInfo[1].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}			
	}
	else if(pwInfo.PowerType == DOVDD)
	{
		if(PowerCustList.PowerCustInfo[2].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			PK_DBG("[CAMERA SENSOR] DOVDD power on");
			if(TRUE != hwPowerOn(pwInfo.PowerType,pwInfo.Voltage,mode_name))
			{
				PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
				return FALSE;
			}
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[2].Gpio_Pin,PowerCustList.PowerCustInfo[2].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[2].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[2].Gpio_Pin,PowerCustList.PowerCustInfo[2].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}			
	}
	else if(pwInfo.PowerType == AFVDD)
	{
		if(PowerCustList.PowerCustInfo[3].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			PK_DBG("[CAMERA SENSOR] AFVDD power on");
			if(TRUE != hwPowerOn(pwInfo.PowerType,pwInfo.Voltage,mode_name))
			{
				PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
				return FALSE;
			}
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[3].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}

			if(PowerCustList.PowerCustInfo[4].Gpio_Pin != GPIO_UNSUPPORTED)
			{
				if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
				if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[3].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
				if(mt_set_gpio_out(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
			}	
		}			
	}
	else if(pwInfo.PowerType==PDN)
	{
		PK_DBG("PDN %d ON\n",pwInfo.Voltage);

		if(mt_set_gpio_mode(pinSet[pinSetIdx][IDX_PS_CMPDN],pinSet[pinSetIdx][IDX_PS_CMPDN+IDX_PS_MODE])){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN],GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}
		if(pwInfo.Voltage == Vol_High)
		{			
			if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMPDN],pinSet[pinSetIdx][IDX_PS_CMPDN+IDX_PS_ON])){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}
		else
		{			
			if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMPDN],pinSet[pinSetIdx][IDX_PS_CMPDN+IDX_PS_OFF])){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}
	}
	else if(pwInfo.PowerType==RST)
	{
		PK_DBG("RST %d ON\n",pwInfo.Voltage);

		if(pinSetIdx==0)
		{
#ifndef MTK_MT6306_SUPPORT
			if(mt_set_gpio_mode(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_MODE])){PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");}
			if(mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST],GPIO_DIR_OUT)){PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");}
			if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");}
			if(pwInfo.Voltage == Vol_High)
			{			
				if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_ON])){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
			}
			else
			{			
				if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
			}
#else
			if(mt6306_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST],GPIO_DIR_OUT)){PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");}
			if(pwInfo.Voltage == Vol_High)
			{
				if(mt6306_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_ON])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");} 				 
			}
			else{
				if(mt6306_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");} 	
			}
#endif 
		}
		else if(pinSetIdx==1)
		{
			if(mt_set_gpio_mode(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_MODE])){PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");}
			if(mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST],GPIO_DIR_OUT)){PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");}
			if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");}
			if(pwInfo.Voltage == Vol_High)
			{			
				if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_ON])){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
			}
			else
			{			
				if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
			}
		}


	}
	else if(pwInfo.PowerType==SensorMCLK)
	{
		PK_DBG("[CAMERA SENSOR] %d sensor MCLK ON\n",pinSetIdx);
		if(pinSetIdx==0)
		{
			ISP_MCLK1_EN(TRUE);
		}
		else if(pinSetIdx==1)
		{
			ISP_MCLK1_EN(TRUE);
		}
	}
	else{}
	if(pwInfo.Delay>0)
		mdelay(pwInfo.Delay);
	return TRUE;
}



BOOL hwpowerdown(PowerInformation pwInfo, char* mode_name)
{
	if(pwInfo.PowerType == AVDD)
	{
		if(PowerCustList.PowerCustInfo[0].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			if(TRUE != hwPowerDown(pwInfo.PowerType,mode_name))
			{
				PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
				return FALSE;
			}
			PK_DBG("[CAMERA SENSOR] AVDD OFF\n");
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[0].Gpio_Pin,PowerCustList.PowerCustInfo[0].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[0].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[0].Gpio_Pin,PowerCustList.PowerCustInfo[0].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}			
	}
	else if(pwInfo.PowerType == DVDD)
	{
		if(PowerCustList.PowerCustInfo[1].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			if(pinSetIdx==1)
			{
				if(TRUE != hwPowerDown(PMIC_APP_SUB_CAMERA_POWER_D,mode_name))
				{
					PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
					return FALSE;
				}
				PK_DBG("[CAMERA SENSOR]SUB CAMERA DVDD OFF\n");
			}
			else if(pinSetIdx==0)
			{
				if(TRUE != hwPowerDown(pwInfo.PowerType,mode_name)){
					PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
					return FALSE;
				}
				PK_DBG("[CAMERA SENSOR]MAIN CAMERA DVDD OFF\n");
			}
			else{}
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[1].Gpio_Pin,PowerCustList.PowerCustInfo[1].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[1].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[1].Gpio_Pin,PowerCustList.PowerCustInfo[1].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}			
	}
	else if(pwInfo.PowerType == DOVDD)
	{
		if(PowerCustList.PowerCustInfo[2].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			if(TRUE != hwPowerDown(pwInfo.PowerType,mode_name))
			{
				PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
				return FALSE;
			}
			PK_DBG("[CAMERA SENSOR] DOVDD OFF\n");
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[2].Gpio_Pin,PowerCustList.PowerCustInfo[2].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[2].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[2].Gpio_Pin,PowerCustList.PowerCustInfo[2].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		}			
	}
	else if(pwInfo.PowerType == AFVDD)
	{
		if(PowerCustList.PowerCustInfo[3].Gpio_Pin == GPIO_UNSUPPORTED)
		{
			if(TRUE != hwPowerDown(pwInfo.PowerType,mode_name))
			{
				PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
				return FALSE;
			}
			PK_DBG("[CAMERA SENSOR] AFVDD OFF\n");
		}
		else{
			if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
			if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[3].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
			if(mt_set_gpio_out(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}

			if(PowerCustList.PowerCustInfo[4].Gpio_Pin != GPIO_UNSUPPORTED)
			{
				if(mt_set_gpio_mode(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Gpio_Mode)){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
				if(mt_set_gpio_dir(PowerCustList.PowerCustInfo[3].Gpio_Pin,GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}						
				if(mt_set_gpio_out(PowerCustList.PowerCustInfo[3].Gpio_Pin,PowerCustList.PowerCustInfo[3].Voltage)){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
			}	
		}			
	}
	else if(pwInfo.PowerType==PDN)
	{

		if(mt_set_gpio_mode(pinSet[pinSetIdx][IDX_PS_CMPDN],pinSet[pinSetIdx][IDX_PS_CMPDN+IDX_PS_MODE])){PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");}
		if(mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN],GPIO_DIR_OUT)){PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");}
		if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMPDN],pinSet[pinSetIdx][IDX_PS_CMPDN+IDX_PS_OFF])){PK_DBG("[CAMERA LENS] set gpio failed!! \n");}
		PK_DBG("[CAMERA SENSOR] PDN pwInfo.Voltage=%d pin=%x value=%d OFF\n",pwInfo.Voltage,pinSet[pinSetIdx][IDX_PS_CMPDN],pinSet[pinSetIdx][IDX_PS_CMPDN+IDX_PS_OFF]);
	}
	else if(pwInfo.PowerType==RST)
	{
		
		if(pinSetIdx==0)
		{
#ifndef MTK_MT6306_SUPPORT
			if(mt_set_gpio_mode(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_MODE])){PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");}
			if(mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST],GPIO_DIR_OUT)){PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");}
			if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");}
			PK_DBG("[CAMERA SENSOR] Main RST pwInfo.Voltage=%d pin=%x value=%d OFF\n",pwInfo.Voltage,pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF]);
#else
			if(mt6306_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST],GPIO_DIR_OUT)){PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");}
			if(pwInfo.Voltage == Vol_High)
			{
				if(mt6306_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_ON])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");}				 
			}
			else{
				if(mt6306_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");} 	
			}
#endif 
		}
		else if(pinSetIdx==1)
		{
			if(mt_set_gpio_mode(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_MODE])){PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");}
			if(mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST],GPIO_DIR_OUT)){PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");}
			if(mt_set_gpio_out(pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF])){PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");}	
			PK_DBG("[CAMERA SENSOR] Sub RST pwInfo.Voltage=%d pin=%x value=%d OFF\n",pwInfo.Voltage,pinSet[pinSetIdx][IDX_PS_CMRST],pinSet[pinSetIdx][IDX_PS_CMRST+IDX_PS_OFF]);
		}

	}
	else if(pwInfo.PowerType==SensorMCLK)
	{
		PK_DBG("[CAMERA SENSOR] %d sensor MCLK OFF\n",pinSetIdx);
		if(pinSetIdx==0)
		{
			ISP_MCLK1_EN(FALSE);
		}
		else if(pinSetIdx==1)
		{
			ISP_MCLK1_EN(FALSE);
		}
	}
	else{}
	if(pwInfo.Delay>0)
		mdelay(pwInfo.Delay);
	return TRUE;
}




int kdCISModulePowerOn(CAMERA_DUAL_CAMERA_SENSOR_ENUM SensorIdx, char *currSensorName, BOOL On, char* mode_name)
{

	int pwListIdx,pwIdx;
	BOOL sensorInPowerList = KAL_FALSE;
	
	printk("yao,enter %s\n",__func__);

	if (DUAL_CAMERA_MAIN_SENSOR == SensorIdx){
		pinSetIdx = 0;
	}
	else if (DUAL_CAMERA_SUB_SENSOR == SensorIdx) {
		pinSetIdx = 1;
	}
	else if (DUAL_CAMERA_MAIN_2_SENSOR == SensorIdx) {
		pinSetIdx = 2;
	}

	//power ON
	if (On) {
		PK_DBG("kdCISModulePowerOn -on:currSensorName=%s\n",currSensorName);
		PK_DBG("kdCISModulePowerOn -on:pinSetIdx=%d\n",pinSetIdx);

		for(pwListIdx=0 ; pwListIdx<16; pwListIdx++)
		{
			if(currSensorName && (PowerOnList.PowerSeq[pwListIdx].SensorName!=NULL) && (0 == strcmp(PowerOnList.PowerSeq[pwListIdx].SensorName,currSensorName)))
			{
				PK_DBG("kdCISModulePowerOn get in--- \n");
				PK_DBG("sensorIdx:%d \n",SensorIdx);

				sensorInPowerList = KAL_TRUE;

				for(pwIdx=0;pwIdx<10;pwIdx++)
				{  
					if(PowerOnList.PowerSeq[pwListIdx].PowerInfo[pwIdx].PowerType != VDD_None)
					{
						if(hwpoweron(PowerOnList.PowerSeq[pwListIdx].PowerInfo[pwIdx],mode_name)==FALSE)
							goto _kdCISModulePowerOn_exit_;
					}					
					else
					{
						PK_DBG("pwIdx=%d \n",pwIdx);
						break;
					}
				}
				break;
			}
			else if(PowerOnList.PowerSeq[pwListIdx].SensorName == NULL)
			{	
				break;
			}
			else{}
		}
		if(KAL_FALSE == sensorInPowerList)
			PK_DBG("kdCISModulePowerOn -on:currSensorName=%s poweron seq not defined fatal error\n",currSensorName);

#if 0
	//disable following code for cts 2-camera preview issue
		//make sure the other sensor is power down
		if (pinSetIdx == 0){
			if(mt_set_gpio_mode(pinSet[1][IDX_PS_CMRST],pinSet[1][IDX_PS_CMRST+IDX_PS_MODE])){
				PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
			}
                        if(mt_set_gpio_dir(pinSet[1][IDX_PS_CMRST],GPIO_DIR_OUT)){
				PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
			}
                        if(mt_set_gpio_out(pinSet[1][IDX_PS_CMRST],pinSet[1][IDX_PS_CMRST+IDX_PS_OFF])){
				PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
			}
			//according to spec, pwndn should be pulled down >10ms after reset pin, but here
			//we suppose the shutdown/rst pin is already low level new, check me later	
				
			if(mt_set_gpio_mode(pinSet[1][IDX_PS_CMPDN],pinSet[1][IDX_PS_CMPDN+IDX_PS_MODE])){
				PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");
			}
                	if(mt_set_gpio_dir(pinSet[1][IDX_PS_CMPDN],GPIO_DIR_OUT)){
				PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");
			}
                	if(mt_set_gpio_out(pinSet[1][IDX_PS_CMPDN],pinSet[1][IDX_PS_CMPDN+IDX_PS_OFF])){
				PK_DBG("[CAMERA LENS] set gpio failed!! \n");
			}


			PK_DBG("Power down front sensor as main sensor is on\n");
		} else if (pinSetIdx == 1){
			if(mt_set_gpio_mode(pinSet[0][IDX_PS_CMPDN],pinSet[0][IDX_PS_CMPDN+IDX_PS_MODE])){
				PK_DBG("[CAMERA LENS] set gpio mode failed!! \n");
			}
                	if(mt_set_gpio_dir(pinSet[0][IDX_PS_CMPDN],GPIO_DIR_OUT)){
				PK_DBG("[CAMERA LENS] set gpio dir failed!! \n");
			}
                	if(mt_set_gpio_out(pinSet[0][IDX_PS_CMPDN],pinSet[0][IDX_PS_CMPDN+IDX_PS_OFF])){
				PK_DBG("[CAMERA LENS] set gpio failed!! \n");
			}

			PK_DBG("Power down main sensor as front sensor is on\n");
		}
#endif
	}
	else {//power OFF
		PK_DBG("kdCISModulePowerOn -off:currSensorName=%s\n",currSensorName);
		PK_DBG("kdCISModulePowerOn -off:pinSetIdx=%d\n",pinSetIdx);

		for(pwListIdx=0 ; pwListIdx<16; pwListIdx++)
		{
			if(currSensorName && (PowerDownList.PowerSeq[pwListIdx].SensorName!=NULL) && (0 == strcmp(PowerDownList.PowerSeq[pwListIdx].SensorName,currSensorName)))
			{
				PK_DBG("kdCISModulePowerOff get in--- \n");
				PK_DBG("sensorIdx:%d \n",SensorIdx);

				sensorInPowerList = KAL_TRUE;

				for(pwIdx=9;pwIdx>=0;pwIdx--)
				{  
					if(PowerDownList.PowerSeq[pwListIdx].PowerInfo[pwIdx].PowerType != VDD_None)
					{
						if(hwpowerdown(PowerDownList.PowerSeq[pwListIdx].PowerInfo[pwIdx],mode_name)==FALSE)
							goto _kdCISModulePowerOn_exit_;
						
					}					
					else
					{
						PK_DBG("pwIdx=%d \n",pwIdx);
					}
				}
			}
			else if(PowerDownList.PowerSeq[pwListIdx].SensorName == NULL)
			{	
				break;
			}
			else{}
		}
		// Temp solution: default power on/off sequence
		if(KAL_FALSE == sensorInPowerList)
			PK_DBG("kdCISModulePowerOn -off:currSensorName=%s poweron seq not defined fatal error\n",currSensorName);
	}//

	return 0;

_kdCISModulePowerOn_exit_:
	return -EIO;
}

EXPORT_SYMBOL(kdCISModulePowerOn);


//!--
//




