#ifndef DUT_TEST_MAIN_H
#define DUT_TEST_MAIN_H

#include <stdint.h>
/* Minimal HAL boundary for exercising the real foreground runtime on a host.
   No MCU register layout or electrical/timing behavior is simulated. */
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef struct { uint32_t BSRR; } GPIO_TypeDef;
typedef struct { void *Instance; } ADC_HandleTypeDef;
typedef struct { void *Instance; } TIM_HandleTypeDef;
extern GPIO_TypeDef test_gpio;
extern uint32_t test_timer_counter, test_timer_cleared;
extern int test_timer_instance;
#define TIM2 ((void *)&test_timer_instance)
#define TIM2_IRQn 28
#define TIM_IT_UPDATE 1U
#define HEATER_CMD_GPIO_Port (&test_gpio)
#define HEATER_CMD_Pin 1U
#define PLANT_VALID_GPIO_Port (&test_gpio)
#define PLANT_VALID_Pin 2U
#define HAL_ADC_ERROR_NONE 0U
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_TIM_SET_COUNTER(timer, value) ((void)(timer), test_timer_counter = (value))
#define __HAL_TIM_CLEAR_IT(timer, value) ((void)(timer), test_timer_cleared = (value))
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
uint32_t HAL_GetTick(void);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc);
uint32_t HAL_ADC_GetError(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *timer);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *timer);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer);
void Error_Handler(void);

#endif
