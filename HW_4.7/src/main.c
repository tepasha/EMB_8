#include "stm32f4xx.h"          /* CMSIS device header (стандартний для проєкту) */

#define LED_PIN   13u           /* PC13 */
#define BTN_PIN   0u            /* PA0  */

#define MIN_PERIOD_MS  200u
#define MAX_PERIOD_MS  1000u
#define DEBOUNCE_MS    200u

static volatile uint32_t systick_ms   = 0;
static volatile uint32_t last_press   = 0;
static volatile uint32_t rand_state   = 0x1u;

/* ---------- Простий PRNG (xorshift32) ---------- */
static uint32_t xorshift32(void)
{
    uint32_t x = rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand_state = x;
    return x;
}

/* ---------- SysTick: мс-лічильник ---------- */
void SysTick_Handler(void)
{
    systick_ms++;
}

static void SysTick_Init(void)
{
    /* 16 МГц / 1000 = 16000 тіків на 1 мс */
    SysTick->LOAD = (16000000u / 1000u) - 1u;
    SysTick->VAL  = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk   |
                    SysTick_CTRL_ENABLE_Msk;
}

/* ---------- GPIO ---------- */
static void GPIO_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;

    /* PC13 — вихід push-pull, low speed */
    GPIOC->MODER   &= ~(3u << (LED_PIN * 2));
    GPIOC->MODER   |=  (1u << (LED_PIN * 2));   /* 01 = output */
    GPIOC->OTYPER  &= ~(1u << LED_PIN);         /* push-pull */
    GPIOC->OSPEEDR &= ~(3u << (LED_PIN * 2));   /* low speed */
    GPIOC->ODR     |=  (1u << LED_PIN);         /* старт: LED вимкнено (active-low) */

    /* PA0 — вхід з підтяжкою вгору (кнопка замикає на GND) */
    GPIOA->MODER &= ~(3u << (BTN_PIN * 2));     /* 00 = input */
    GPIOA->PUPDR &= ~(3u << (BTN_PIN * 2));
    GPIOA->PUPDR |=  (1u << (BTN_PIN * 2));     /* 01 = pull-up */
}

/* ---------- EXTI0 для PA0 ---------- */
static void EXTI0_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* EXTI0 підключаємо до порту A (значення 0000) */
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI0;

    EXTI->IMR  |= EXTI_IMR_MR0;    /* демаскувати лінію 0 */
    EXTI->FTSR |= EXTI_FTSR_TR0;   /* тригер по спадному фронту (натискання) */
    EXTI->RTSR &= ~EXTI_RTSR_TR0;

    NVIC_SetPriority(EXTI0_IRQn, 1);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void EXTI0_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR0)
    {
        EXTI->PR = EXTI_PR_PR0;   /* очистити прапорець (запис 1) */

        /* програмний дебаунс */
        if ((systick_ms - last_press) > DEBOUNCE_MS)
        {
            last_press = systick_ms;

            /* реальність натискання людиною — непогане джерело "розкиду" */
            rand_state ^= systick_ms;

            uint32_t new_period = MIN_PERIOD_MS +
                                   (xorshift32() % (MAX_PERIOD_MS - MIN_PERIOD_MS + 1u));

            TIM2->ARR = new_period - 1u;
            TIM2->EGR |= TIM_EGR_UG;   /* форсоване оновлення (перезавантажити CNT/ARR) */
        }
    }
}

/* ---------- TIM2: генератор періоду блимання ---------- */
static void TIM2_Init(uint32_t period_ms)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    TIM2->PSC = 16000u - 1u;      /* 16 МГц / 16000 = 1 кГц (1 тік = 1 мс) */
    TIM2->ARR = period_ms - 1u;
    TIM2->CNT = 0u;

    TIM2->CR1  |= TIM_CR1_ARPE;
    TIM2->DIER |= TIM_DIER_UIE;

    NVIC_SetPriority(TIM2_IRQn, 2);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM2->EGR |= TIM_EGR_UG;      /* застосувати PSC/ARR одразу */
    TIM2->CR1 |= TIM_CR1_CEN;     /* старт таймера */
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF)
    {
        TIM2->SR &= ~TIM_SR_UIF;
        GPIOC->ODR ^= (1u << LED_PIN);   /* тумблер LED */
    }
}

/* ---------- main ---------- */
int main(void)
{
    GPIO_Init();
    SysTick_Init();
    EXTI0_Init();
    TIM2_Init(500u);   /* стартовий період — 500 мс */

    rand_state ^= 0xA5A5A5A5u;

    while (1)
    {
        __WFI();       /* спати до наступного переривання (TIM2 або EXTI0) */
    }
}
