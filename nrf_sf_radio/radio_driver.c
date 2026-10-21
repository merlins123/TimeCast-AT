/*
 * SPDX-FileCopyrightText: 2026 Xin He
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     sys_nrf_sf_radio
 * @{
 *
 * @file
 * @brief       Physical layer radio, timer and PPI configuration
 *
 * @author      Xin He <xin.he@mailbox.tu-dresden.de>
 *
 * @}
 */

#include <stddef.h>

#include "nrf_clock.h"
#include "radio_driver.h"
#include "radio_internal.h"
#include "syscfg/syscfg.h"
#include "controller/ble_phy.h"

static void _timer2_init(void);
static int _timer_ppi_init(void);
static void _timer2_reset_tx(void);
static void _timer2_reset_rx(void);
static void _init_radio(void);

static const uint8_t ble_hw_frequency_channels[40] = {
    4,  6,  8, 10, 12, 14, 16, 18, 20, 22,  /* 0-9 */
    24, 28, 30, 32, 34, 36, 38, 40, 42, 44, /* 10-19 */
    46, 48, 50, 52, 54, 56, 58, 60, 62, 64, /* 20-29 */
    66, 68, 70, 72, 74, 76, 78,  2, 26, 80, /* 30-39 */
};

#ifndef NRF_SF_RADIO_ACCESS_ADDRESS
#  define NRF_SF_RADIO_ACCESS_ADDRESS (0x8389BED6u)
#endif

#define READY_TIME_PPI_CH   (9U)
#define END_TIME_PPI_CH     (10U)
#define RADIO_START_PPI_CH  (11U)
#define TIMER2_START_PPI_CH (12U)
#define RX_EN_PPI_CH        (13U)
#define TX_EN_PPI_CH        (15U)
#define ADDRESS_TIME_PPI_CH (14U)

static int _ppi_anchor_inited;

static void _timer2_init(void)
{
    NRF_TIMER2->TASKS_SHUTDOWN = 1;

    NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer;
    NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER2->PRESCALER = 0;
    NRF_TIMER2->TASKS_CLEAR = 1;

    NRF_TIMER2->EVENTS_COMPARE[0] = 0;
    NRF_TIMER2->INTENCLR = 0xffffffffUL;

    NRF_TIMER2->SHORTS = TIMER_SHORTS_COMPARE0_STOP_Msk |
                         TIMER_SHORTS_COMPARE0_CLEAR_Msk;

    NRF_PPI->CH[TIMER2_START_PPI_CH].EEP =
        (uint32_t)&NRF_TIMER3->EVENTS_COMPARE[0];
    NRF_PPI->CH[TIMER2_START_PPI_CH].TEP =
        (uint32_t)&NRF_TIMER2->TASKS_START;

    NRF_PPI->CH[RADIO_START_PPI_CH].EEP =
        (uint32_t)&NRF_TIMER2->EVENTS_COMPARE[0];
    NRF_PPI->CH[RADIO_START_PPI_CH].TEP = (uint32_t)&NRF_RADIO->TASKS_START;

    NRF_PPI->CHENSET = (1UL << TIMER2_START_PPI_CH) |
                       (1UL << RADIO_START_PPI_CH);
}

static int _timer_ppi_init(void)
{
    if (_ppi_anchor_inited) {
        return 0;
    }

    NRF_TIMER3->MODE = TIMER_MODE_MODE_Timer;
    NRF_TIMER3->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER3->PRESCALER = 0;
    NRF_TIMER3->TASKS_CLEAR = 1;
    NRF_TIMER3->TASKS_START = 1;

    NRF_PPI->CH[TX_EN_PPI_CH].EEP = (uint32_t)&NRF_TIMER3->EVENTS_COMPARE[0];
    NRF_PPI->CH[TX_EN_PPI_CH].TEP = (uint32_t)&NRF_RADIO->TASKS_TXEN;

    NRF_PPI->CH[RX_EN_PPI_CH].EEP = (uint32_t)&NRF_TIMER3->EVENTS_COMPARE[0];
    NRF_PPI->CH[RX_EN_PPI_CH].TEP = (uint32_t)&NRF_RADIO->TASKS_RXEN;

    NRF_PPI->CH[ADDRESS_TIME_PPI_CH].EEP = (uint32_t)&NRF_RADIO->EVENTS_ADDRESS;
    NRF_PPI->CH[ADDRESS_TIME_PPI_CH].TEP = (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[1];
    NRF_PPI->CHENSET = (1UL << ADDRESS_TIME_PPI_CH);

    NRF_PPI->CH[END_TIME_PPI_CH].EEP = (uint32_t)&NRF_RADIO->EVENTS_END;
    NRF_PPI->CH[END_TIME_PPI_CH].TEP = (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[3];
    NRF_PPI->CHENSET = (1UL << END_TIME_PPI_CH);

    NRF_PPI->CH[READY_TIME_PPI_CH].EEP = (uint32_t)&NRF_RADIO->EVENTS_READY;
    NRF_PPI->CH[READY_TIME_PPI_CH].TEP = (uint32_t)&NRF_TIMER3->TASKS_CAPTURE[4];
    NRF_PPI->CHENSET = (1UL << READY_TIME_PPI_CH);

    _timer2_init();
    _ppi_anchor_inited = 1;

    return 0;
}

void nrf_sf_radio_wait_until(volatile uint32_t *event, uint32_t timeout_ticks)
{
    uint32_t start_ticks = nrf_sf_radio_now_ticks();

    nrf_sf_radio_wait_until_abs(event, start_ticks + timeout_ticks);
}

void nrf_sf_radio_wait_until_abs(volatile uint32_t *event, uint32_t deadline)
{
    while (((event == NULL) || (*event == 0U)) &&
           ((int32_t)(nrf_sf_radio_now_ticks() - deadline) < 0)) {}
}

uint32_t nrf_sf_radio_get_last_ready_time_ticks(void)
{
    return NRF_TIMER3->CC[4];
}

uint32_t nrf_sf_radio_get_last_end_time_ticks(void)
{
    return NRF_TIMER3->CC[3];
}

uint32_t nrf_sf_radio_get_last_address_time_ticks(void)
{
    return NRF_TIMER3->CC[1];
}

uint32_t nrf_sf_radio_now_ticks(void)
{
    NRF_TIMER3->TASKS_CAPTURE[2] = 1;
    return NRF_TIMER3->CC[2];
}

static void _timer2_reset_tx(void)
{
    NRF_PPI->CHENSET = (1UL << TX_EN_PPI_CH);
    NRF_PPI->CHENCLR = (1UL << RX_EN_PPI_CH);

    NRF_TIMER2->TASKS_SHUTDOWN = 1;
    NRF_TIMER2->EVENTS_COMPARE[0] = 0;
    NRF_TIMER2->CC[0] = NRF_SF_RADIO_RAMPUP_TIME_TICKS;

    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_RADIO->EVENTS_DISABLED = 0U;
    NRF_RADIO->TASKS_DISABLE = 1U;

    NRF_RADIO->EVENTS_END = 0U;
    NRF_RADIO->EVENTS_ADDRESS = 0U;
    NRF_RADIO->EVENTS_READY = 0U;

    nrf_sf_radio_wait_until(&NRF_RADIO->EVENTS_DISABLED,
                            NRF_SF_RADIO_US_TO_TIMER_TICKS(2U));
    NRF_RADIO->EVENTS_DISABLED = 0U;
}

void nrf_sf_radio_tx_arm(uint8_t *buf, uint32_t deadline_ticks)
{
    _timer2_reset_tx();
    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_TIMER3->CC[0] = deadline_ticks;
    NRF_RADIO->PACKETPTR = (uint32_t)buf;
}

static void _timer2_reset_rx(void)
{
    NRF_PPI->CHENSET = (1UL << RX_EN_PPI_CH);
    NRF_PPI->CHENCLR = (1UL << TX_EN_PPI_CH);

    NRF_TIMER2->TASKS_SHUTDOWN = 1;
    NRF_TIMER2->EVENTS_COMPARE[0] = 0;
    NRF_TIMER2->CC[0] = NRF_SF_RADIO_RAMPUP_TIME_TICKS;

    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_RADIO->EVENTS_DISABLED = 0U;
    NRF_RADIO->TASKS_DISABLE = 1U;

    NRF_RADIO->EVENTS_END = 0U;
    NRF_RADIO->EVENTS_ADDRESS = 0U;
    NRF_RADIO->EVENTS_READY = 0U;

    nrf_sf_radio_wait_until(&NRF_RADIO->EVENTS_DISABLED,
                            NRF_SF_RADIO_US_TO_TIMER_TICKS(2U));
    NRF_RADIO->EVENTS_DISABLED = 0U;
}

void nrf_sf_radio_rx_arm(uint8_t *buf, uint32_t deadline_ticks)
{
    _timer2_reset_rx();
    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_TIMER3->CC[0] = deadline_ticks;
    NRF_RADIO->PACKETPTR = (uint32_t)buf;
}

void nrf_sf_radio_try_rx_enable(uint8_t *buf)
{
    NRF_RADIO->EVENTS_DISABLED = 0U;
    NRF_RADIO->TASKS_DISABLE = 1;
    nrf_sf_radio_wait_until(&NRF_RADIO->EVENTS_DISABLED,
                            NRF_SF_RADIO_US_TO_TIMER_TICKS(2U));

    NRF_TIMER2->TASKS_SHUTDOWN = 1;
    NRF_TIMER2->EVENTS_COMPARE[0] = 0;
    NRF_TIMER2->CC[0] = NRF_SF_RADIO_RAMPUP_TIME_TICKS;

    NRF_RADIO->EVENTS_READY = 0U;
    NRF_RADIO->EVENTS_DISABLED = 0U;
    NRF_RADIO->EVENTS_END = 0U;
    NRF_RADIO->EVENTS_ADDRESS = 0U;
    NRF_RADIO->EVENTS_PAYLOAD = 0U;

    NRF_RADIO->PACKETPTR = (uint32_t)buf;

    NRF_RADIO->TASKS_RXEN = 1;
    NRF_TIMER2->TASKS_START = 1;
}

void nrf_sf_radio_set_mode(uint32_t mode)
{
    NRF_RADIO->MODE = mode;
}

void nrf_sf_radio_set_ble_channel(uint8_t channel)
{
    NRF_RADIO->FREQUENCY = ble_hw_frequency_channels[channel];
    NRF_RADIO->DATAWHITEIV = channel;
}

void nrf_sf_radio_set_power(int16_t power)
{
    ble_phy_tx_power_set(power);
}

static void _init_radio(void)
{
    ble_phy_rfclk_enable();

    ble_phy_init();

    NVIC_DisableIRQ(RADIO_IRQn);
    ble_phy_disable();
    NRF_RADIO->INTENCLR = 0xffffffffUL;
    NRF_PPI->CHENCLR = PPI_CHEN_CH26_Msk | PPI_CHEN_CH27_Msk;


    ble_phy_tx_power_set(0);
    ble_phy_setchan(37, NRF_SF_RADIO_ACCESS_ADDRESS, 0x555555);
    NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk;
}

void nrf_sf_radio_start(void)
{
    _init_radio();
    _timer_ppi_init();
}
