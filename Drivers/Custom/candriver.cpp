#include "system_configuration.h"
#include "FreeRTOS_wrapper.h"

#include "candriver.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_can.h"
#include "stm32f4xx_hal_cortex.h"

#define CANx                            CAN1
#define CANx_CLK_ENABLE()               __HAL_RCC_CANx_CLK_ENABLE()
#define CANx_GPIO_CLK_ENABLE()          __HAL_RCC_GPIOD_CLK_ENABLE()

#define CANx_FORCE_RESET()              __HAL_RCC_CANx_FORCE_RESET()
#define CANx_RELEASE_RESET()            __HAL_RCC_CANx_RELEASE_RESET()

#define CANx_TX_PIN                    GPIO_PIN_1
#define CANx_TX_GPIO_PORT              GPIOD
#define CANx_TX_AF                     GPIO_AF9_CAN1
#define CANx_RX_PIN                    GPIO_PIN_0
#define CANx_RX_GPIO_PORT              GPIOD
#define CANx_RX_AF                     GPIO_AF9_CAN1

COMMON can_driver_t CAN_driver; //!< singleton CAN driver object

extern "C" QueueHandle_t get_RX_queue( void)
{
	return CAN_driver.get_RX_Queue().get_queue();
}

bool can_driver_t::send_can_packet (const CANpacket &msg)
{
  uint8_t transmitmailbox;

  /* Select one empty transmit mailbox */
  if ((CANx->TSR & CAN_TSR_TME0) == CAN_TSR_TME0)
    transmitmailbox = 0;
  else if ((CANx->TSR & CAN_TSR_TME1) == CAN_TSR_TME1)
    transmitmailbox = 1;
  else if ((CANx->TSR & CAN_TSR_TME2) == CAN_TSR_TME2)
    transmitmailbox = 2;
  else
    return false; // give up, no mailbox empty

  /* Set up the Id */
  CANx->sTxMailBox[transmitmailbox].TIR &= CAN_TI0R_TXRQ;
  CANx->sTxMailBox[transmitmailbox].TIR |= msg.id << 21;

  /* Set up the DLC */
  CANx->sTxMailBox[transmitmailbox].TDTR &= (uint32_t) 0xFFFFFFF0;
  CANx->sTxMailBox[transmitmailbox].TDTR |= msg.dlc;

  /* Set up the data field */
  CANx->sTxMailBox[transmitmailbox].TDLR = msg.data_w[0];
  CANx->sTxMailBox[transmitmailbox].TDHR = msg.data_w[1];

  /* Request transmission */
  CANx->sTxMailBox[transmitmailbox].TIR |= CAN_TI0R_TXRQ;
  return true;
}

namespace CAN_driver_ISR
{
  /**
   * @brief  This function handles CANx RX0 interrupt request.
   * @param  None
   * @retval None
   */
  extern "C" void CAN1_RX0_IRQHandler (void)
  {
    CANpacket msg;

    msg.id = 0x07FF & (uint16_t) (CANx->sFIFOMailBox[0].RIR >> 21);
    msg.dlc = (uint8_t) 0x0F & CANx->sFIFOMailBox[0].RDTR;
    msg.data_w[0] = CANx->sFIFOMailBox[0].RDLR;
    msg.data_w[1] = CANx->sFIFOMailBox[0].RDHR;

    BaseType_t xHigherPriorityTaskWoken = false;
    bool result = CAN_driver.RX_queue.send_from_ISR (msg);
    ASSERT(result == true); // trap for RX queue overrun

    CANx->RF0R |= CAN_RF0R_RFOM0; // release FIFO 0

    portEND_SWITCHING_ISR (xHigherPriorityTaskWoken);
  }

  extern "C" void CAN1_TX_IRQHandler (void)
  {
    CANpacket msg;
    BaseType_t xHigherPriorityTaskWoken = false;
    if (CAN_driver.TX_queue.receive_from_ISR (msg))
      CAN_driver.send_can_packet (msg);
    else
      CANx->IER &= ~CAN_IT_TX_MAILBOX_EMPTY; // interrupt off, no more work to do

    portEND_SWITCHING_ISR (xHigherPriorityTaskWoken);
  }

} // namespace CAN_driver_ISR

CAN_HandleTypeDef CanHandle;

can_driver_t::can_driver_t () :
    RX_queue (20),
    TX_queue (20)
{
  __HAL_RCC_CAN1_CLK_ENABLE();  // also required for CAN2 !!
//  __HAL_RCC_CAN2_CLK_ENABLE();

  /* Enable GPIO clock ****************************************/
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*##-2- Configure peripheral GPIO ##########################################*/
  /* CAN1 TX GPIO pin configuration */
  GPIO_InitTypeDef GPIO_InitStruct =
    { 0 };
  GPIO_InitStruct.Pin = CANx_TX_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Alternate = CANx_TX_AF;

  HAL_GPIO_Init (CANx_TX_GPIO_PORT, &GPIO_InitStruct);

  /* CAN1 RX GPIO pin configuration */
  GPIO_InitStruct.Pin = CANx_RX_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Alternate = CANx_RX_AF;

  HAL_GPIO_Init (CANx_RX_GPIO_PORT, &GPIO_InitStruct);

  CAN_FilterTypeDef sFilterConfig;

  /*##-1- Configure the CAN peripheral #######################################*/
  CanHandle.Instance = CANx;

  CanHandle.Init.TimeTriggeredMode = DISABLE;
  CanHandle.Init.AutoBusOff = DISABLE;
  CanHandle.Init.AutoWakeUp = DISABLE;
  CanHandle.Init.AutoRetransmission = ENABLE;
  CanHandle.Init.ReceiveFifoLocked = DISABLE;
  CanHandle.Init.TransmitFifoPriority = DISABLE;
  CanHandle.Init.Mode = CAN_MODE_NORMAL;
  CanHandle.Init.SyncJumpWidth = CAN_SJW_1TQ;
  CanHandle.Init.TimeSeg1 = CAN_BS1_4TQ;
  CanHandle.Init.TimeSeg2 = CAN_BS2_2TQ;
  CanHandle.Init.Prescaler = 6;

  if (HAL_CAN_Init (&CanHandle) != HAL_OK)
    {
      /* Initialization Error */
      asm("bkpt 0");
    }

  /*##-2- Configure the CAN Filter ###########################################*/
  sFilterConfig.FilterBank = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0x0000;
  sFilterConfig.FilterIdLow = 0x0000;
  sFilterConfig.FilterMaskIdHigh = 0x0000;
  sFilterConfig.FilterMaskIdLow = 0x0000;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;

  if (HAL_CAN_ConfigFilter (&CanHandle, &sFilterConfig) != HAL_OK)
    {
      /* Filter configuration Error */
      asm("bkpt 0");
    }

  /*##-3- Start the CAN peripheral ###########################################*/
  if (HAL_CAN_Start (&CanHandle) != HAL_OK)
    {
      /* Start Error */
      asm("bkpt 0");
    }

  /*##-4- Activate CAN RX notification #######################################*/
  if (HAL_CAN_ActivateNotification (&CanHandle, CAN_IT_RX_FIFO0_MSG_PENDING)
      != HAL_OK)
    {
      /* Notification Error */
      asm("bkpt 0");
    }

  uint32_t prioritygroup = NVIC_GetPriorityGrouping ();

  NVIC_SetPriority ((IRQn_Type) CAN1_RX0_IRQn,
		    NVIC_EncodePriority (prioritygroup, 15, 0));
  NVIC_EnableIRQ ((IRQn_Type) CAN1_RX0_IRQn);
  NVIC_SetPriority ((IRQn_Type) CAN1_TX_IRQn,
		    NVIC_EncodePriority (prioritygroup, 15, 0));
  NVIC_EnableIRQ ((IRQn_Type) CAN1_TX_IRQn);

  CANx->IER |= CAN_IT_RX_FIFO0_MSG_PENDING; // enable CANx FIFO 0 RX interrupt
}

#if RUN_CAN_TESTER

void can_tester_runnable( void *)
{
	CANpacket TX_packet;
	CANpacket RX_packet;
	TX_packet.id=0x321;
	TX_packet.dlc=8;
	TX_packet.data_l=0;
	while(true)
	{
		CAN_driver.send(TX_packet, 10);
		TX_packet.data_l++;
		delay(50);
		if( CAN_driver.receive(RX_packet, NO_WAIT))
		{
			if( RX_packet.id==0x65)
			{
				RX_packet.id++;
				RX_packet.data_l++;
				CAN_driver.send(RX_packet, 1);
			}
		}
	}
}

RestrictedTask can( can_tester_runnable, "CAN");

#endif
