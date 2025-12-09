#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include "kb.h"
#include "oled.h"
#include "fonts.h"
#include <stdlib.h>
#include <stdio.h>

#define EQUAL '='
#define OPERATOR 'O'
#define ADD '+'
#define SUBTRACT '-'
#define MULTIPLY '*'
#define DIVIDE '/'

enum CalcState {
  READ_FIRST,
  READ_SECOND,
  SHOWING_RESULT
};

struct CalcData {
  int first;
  int second;
  int result;
  char op;
  enum CalcState state;
};

void SystemClock_Config(void);

static void system_init(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART6_UART_Init();
  oled_Init();
}

static const uint8_t ROWS[4] = {ROW4, ROW3, ROW2, ROW1};
static const char KEYBOARD_MAP[4][3] = {
      {OPERATOR, '0', EQUAL},
      {'7','8','9'},
      {'4','5','6'},
      {'1','2','3'}
};

static char resolve_column(uint8_t raw_value, uint8_t row)
{
  switch (raw_value)
  {
    case 0x04: return KEYBOARD_MAP[row][0];
    case 0x02: return KEYBOARD_MAP[row][1];
    case 0x01: return KEYBOARD_MAP[row][2];
    default: return 0;
  }
}

static int add_digit(int base, char digit)
{
  if (digit < '0' || digit > '9') return base;
  if (base < 0) base = 0;
  return base * 10 + (digit - '0');
}

static char next_operator(char op)
{
  switch (op)
  {
    case ADD: return SUBTRACT;
    case SUBTRACT: return MULTIPLY;
    case MULTIPLY: return DIVIDE;
    default: return ADD;
  }
}

static void print_expression(int a, char op, int b)
{
  if (a < 0) 
    return;

  char buff[32];

  oled_Fill(Black);
  oled_UpdateScreen();

  if (op == 0) snprintf(buf, sizeof(buf), "%d", a);
  else if (b == -1) snprintf(buf, sizeof(buf), "%d %c", a, op);
  else snprintf(buf, sizeof(buf), "%d %c %d", a, op, b);

  oled_SetCursor(0,0);
  oled_WriteString(buff, Font_7x10, White);
  oled_UpdateScreen();
}

static void print_result(int value) 
{
  char buf[20];
  snprintf(buf, sizeof(buf), "= %d", value);

  oled_SetCursor(0, 12);
  oled_WriteString(buf, Font_7x10, White);
  oled_UpdateScreen();
}

static char read_keypad(void) 
{
  static uint8_t debounce_latch = 0;

  uint8_t detected = 0;
  uint8_t row = 0xFF;

  for (uint8_t i = 0; i < 4; i++) 
  {
    detected = Check_Row(ROWS[i]);
    if (detected) 
    {
      row = i;
      break;
    }
  }

  if (detected == 0) 
  {
    debounce_latch = 0;
    return 0;
  }

  if (debounce_latch)
    return 0;

  HAL_Delay(15);
  if (Check_Row(ROWS[row]) != detected) 
    return 0;

  debounce_latch = 1;
  return resolve_column(detected, row);
}

static void process_digit_key(struct CalcData *calc, char key)
{
  if (calc->state == SHOWING_RESULT) 
  {
    calc->first = -1;
    calc->second = -1;
    calc->op = 0;
    calc->state = READ_FIRST;
  }

  if (calc->state == READ_FIRST) calc->first = add_digit(calc->first, key);
  else calc->second = add_digit(calc->second, key);

  print_expression(calc->first, calc->op, calc->second);
}

static void process_operator_key(struct CalcData *calc)
{
  if (calc->first < 0)
      return;

  calc->op = next_operator(calc->op);
  calc->state = READ_SECOND;

  print_expression(calc->first, calc->op, calc->second);
}

static void process_equal_key(struct CalcData *calc)
{
  if (calc->first < 0 || calc->op == 0 || calc->second < 0)
    return;

  switch (calc->op) 
  {
    case ADD: calc->result = calc->first + calc->second; break;
    case SUBTRACT: calc->result = calc->first - calc->second; break;
    case MULTIPLY: calc->result = calc->first * calc->second; break;
    case DIVIDE:
      if (calc->second == 0) return;
      calc->result = calc->first / calc->second;
      break;
    default: return;
  }

  print_expression(calc->first, calc->op, calc->second);
  print_result(calc->result);

  calc->first = calc->result;
  calc->second = -1;
  calc->op = 0;
  calc->state = SHOWING_RESULT;
}

int main(void)
{
  system_init();

  struct CalcData calc = {
    .first = -1,
    .second = -1,
    .result = 0,
    .op = 0,
    .state = READ_FIRST
  };

  for (;;)
  {
    char key = read_keypad();
    if (!key) continue;
    else if (key >= '0' && key <= '9') process_digit_key(&calc, key);
    else if (key == OPERATOR) process_operator_key(&calc);
    else if (key == EQUAL) process_equal_key(&calc);
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage 
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{ 
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/