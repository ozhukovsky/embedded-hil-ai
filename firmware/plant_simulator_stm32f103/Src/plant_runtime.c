#include "plant_runtime.h"
#include "hil_board_transport.h"
#include "hil_plant_model.h"
#include "mcp4725.h"
#include "main.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim2;

#define CAPABILITIES ((1U << 1) | (1U << 4) | (1U << 5))
#define TASK_BUDGET_MS 100U
typedef struct {
    hil_protocol_app_t protocol;
    hil_command_cache_t commands;
    hil_plant_model_t model;
    hil_plant_config_t config;
    volatile uint8_t step_pending, step_overlap, step_running;
    uint8_t config_valid, dac_valid, timer_running, fault_latched, tx_fault;
    uint8_t heater_stage, heater_latched;
    uint16_t fault, dac_code;
    uint32_t fault_detail, run_start, next_telemetry, sample_index;
    uint32_t response_drops, telemetry_drops, over_budget, max_cycles;
} plant_t;
static plant_t p;
static uint16_t flags(void);

static hil_codec_status_t send(uint8_t type, const uint8_t *payload, uint16_t length,
                               hil_transport_priority_t priority) {
    hil_codec_status_t result = hil_protocol_app_send_message(&p.protocol, type, payload,
                                                               length, priority);
    if (result != HIL_CODEC_OK) {
        if (priority == HIL_TRANSPORT_CONTROL) { p.tx_fault = 1U; ++p.response_drops; }
        else ++p.telemetry_drops;
    }
    return result;
}

static void sync_status(void) {
    hil_protocol_app_set_status(&p.protocol, p.protocol.state, p.fault,
                                p.config_valid ? p.config.test_id : 0U, flags());
}

static uint16_t flags(void) {
    uint16_t v = 0U;
    if (HAL_GPIO_ReadPin(PLANT_VALID_GPIO_Port, PLANT_VALID_Pin) == GPIO_PIN_SET) v |= 1U;
    if (HAL_GPIO_ReadPin(HEATER_CMD_GPIO_Port, HEATER_CMD_Pin) == GPIO_PIN_SET) v |= 2U;
    if (p.dac_valid) v |= 8U;
    if (p.fault == 5U) v |= 16U;
    if (p.tx_fault) v |= 32U;
    return v;
}

static void nack(const hil_transport_frame_t *f, uint16_t code) {
    hil_ack_t a = {f->sequence, f->message_type, HIL_ROLE_PLANT, code};
    uint8_t b[6]; size_t n = 0U;
    if (hil_encode_nack(&a, b, sizeof(b), &n) == HIL_CODEC_OK) (void)send(HIL_MSG_NACK, b, (uint16_t)n, HIL_TRANSPORT_CONTROL);
}
static int ack_payload(const hil_transport_frame_t *f, uint8_t *b, size_t *n) {
    hil_ack_t a = {f->sequence, f->message_type, HIL_ROLE_PLANT, 0U};
    return hil_encode_ack(&a, b, 6U, n) == HIL_CODEC_OK;
}
static void respond(uint8_t type, const uint8_t *payload, uint16_t length) {
    (void)send(type, payload, length, HIL_TRANSPORT_CONTROL);
}

static void fault_now(uint16_t code, uint16_t detail, uint32_t now) {
    uint8_t was_running, b[13]; size_t n = 0U;
    hil_fault_report_t r;
    hil_test_complete_t c;
    if (p.fault_latched) return;
    p.fault_latched = 1U; p.fault = code; p.fault_detail = detail;
    was_running = (uint8_t)(p.protocol.state == HIL_STATE_RUNNING);
    if (p.timer_running) { (void)HAL_TIM_Base_Stop_IT(&htim2); p.timer_running = 0U; }
    p.step_pending = 0U;
    HAL_GPIO_WritePin(PLANT_VALID_GPIO_Port, PLANT_VALID_Pin, GPIO_PIN_RESET);
    if (code == 2U) p.dac_valid = 0U;
    p.protocol.state = HIL_STATE_FAULT; sync_status();
    r.role = HIL_ROLE_PLANT; r.test_id = p.config_valid ? p.config.test_id : 0U;
    r.fault_code = code; r.fault_detail = detail; r.uptime_ms = now;
    if (hil_encode_fault_report(&r, b, sizeof(b), &n) == HIL_CODEC_OK) respond(HIL_MSG_FAULT_REPORT, b, (uint16_t)n);
    if (was_running) {
        c.role = HIL_ROLE_PLANT; c.test_id = p.config.test_id; c.outcome = HIL_COMPLETE_FAULT_ABORT;
        c.elapsed_ms = now - p.run_start;
        if (hil_encode_test_complete(&c, b, sizeof(b), &n) == HIL_CODEC_OK) respond(HIL_MSG_TEST_COMPLETE, b, (uint16_t)n);
    }
}

static void replay(void) {
    uint8_t i; hil_cached_response_t r;
    for (i=0U;i<p.commands.response_count;++i)
        if (hil_command_cache_get_response(&p.commands,i,&r)==HIL_CODEC_OK) respond(r.message_type,r.payload,r.payload_length);
}

static void config_command(const hil_transport_frame_t *f, uint32_t now) {
    hil_plant_config_t c; hil_plant_model_t model; uint16_t dac; uint8_t ack[6]; size_t ack_n=0U;
    hil_response_message_t response;
    hil_codec_status_t decoded;
    if (f->payload_length && f->payload[0]!=HIL_ROLE_PLANT) { nack(f,HIL_NACK_WRONG_TARGET_ROLE); return; }
    if (p.protocol.state!=HIL_STATE_WAIT_CONFIG && p.protocol.state!=HIL_STATE_READY) { nack(f,HIL_NACK_INVALID_STATE); return; }
    decoded=hil_decode_plant_config(f->payload,f->payload_length,&c);
    if(decoded==HIL_CODEC_INVALID_LENGTH){nack(f,HIL_NACK_INVALID_PAYLOAD_LENGTH);return;}
    if (decoded!=HIL_CODEC_OK || !hil_plant_model_validate(&c) || !hil_plant_model_init(&model,&c)) {
        nack(f,HIL_NACK_INVALID_CONFIGURATION); return;
    }
    if (!hil_plant_temperature_to_dac(c.initial_temp_mC,&dac) || mcp4725_init(&hi2c1)!=HAL_OK || mcp4725_write_verify(&hi2c1,dac)!=HAL_OK) {
        fault_now(2U,(uint16_t)HAL_I2C_GetError(&hi2c1),now); nack(f,HIL_NACK_HARDWARE_NOT_READY); return;
    }
    p.config=c; p.model=model; p.dac_code=dac; p.dac_valid=1U; p.config_valid=1U;
    p.heater_stage=(HAL_GPIO_ReadPin(HEATER_CMD_GPIO_Port,HEATER_CMD_Pin)==GPIO_PIN_RESET)?1U:0U;
    HAL_GPIO_WritePin(PLANT_VALID_GPIO_Port,PLANT_VALID_Pin,GPIO_PIN_SET); p.protocol.state=HIL_STATE_READY;sync_status();
    if (!ack_payload(f,ack,&ack_n)) return;
    response.message_type=HIL_MSG_ACK;response.payload=ack;response.payload_length=(uint16_t)ack_n;
    if (hil_command_cache_commit(&p.commands,f->sequence,f->message_type,f->payload,f->payload_length,&response,1U)==HIL_CODEC_OK)
        respond(response.message_type,response.payload,response.payload_length);
}

static void command(const hil_transport_frame_t *f, uint32_t now) {
    hil_command_result_t d; hil_test_command_t t; uint8_t a[6],second[10]; size_t an=0U,sn=0U;
    hil_response_message_t responses[2]; uint8_t count=1U;
    if (f->message_type!=HIL_MSG_CONFIG_SET && f->message_type!=HIL_MSG_ARM_TEST &&
        f->message_type!=HIL_MSG_START_TEST && f->message_type!=HIL_MSG_STOP_TEST) {
        nack(f,HIL_NACK_UNSUPPORTED_MESSAGE);return;
    }
    if (hil_command_cache_check(&p.commands,f->sequence,f->message_type,f->payload,f->payload_length,&d)!=HIL_CODEC_OK) return;
    if (d==HIL_COMMAND_DUPLICATE) { replay(); return; }
    if (d==HIL_COMMAND_SEQUENCE_CONFLICT) { nack(f,HIL_NACK_SEQUENCE_CONFLICT); return; }
    if (f->message_type==HIL_MSG_CONFIG_SET) { config_command(f,now); return; }
    if (f->message_type==HIL_MSG_ARM_TEST || f->message_type==HIL_MSG_START_TEST) {
        if (f->payload_length!=5U) { nack(f,HIL_NACK_INVALID_PAYLOAD_LENGTH); return; }
        if (f->payload[0]!=HIL_ROLE_PLANT) { nack(f,HIL_NACK_WRONG_TARGET_ROLE); return; }
        if (hil_decode_test_command(f->payload,f->payload_length,&t)!=HIL_CODEC_OK) { nack(f,HIL_NACK_INVALID_PAYLOAD_LENGTH); return; }
        if (t.test_id!=p.config.test_id || !p.config_valid) { nack(f,HIL_NACK_TEST_ID_MISMATCH); return; }
        if (f->message_type==HIL_MSG_ARM_TEST) {
            hil_armed_t armed;
            if (p.protocol.state!=HIL_STATE_READY) { nack(f,HIL_NACK_INVALID_STATE); return; }
            if (!p.dac_valid || p.heater_stage<3U) { nack(f,HIL_NACK_HARDWARE_NOT_READY); return; }
            if (!ack_payload(f,a,&an)) return;
            armed.role=HIL_ROLE_PLANT;armed.test_id=t.test_id;armed.readiness_flags=0x0017U;
            if (hil_encode_armed(&armed,second,sizeof(second),&sn)!=HIL_CODEC_OK) return;
            p.protocol.state=HIL_STATE_ARMED;sync_status();count=2U;
            responses[1].message_type=HIL_MSG_ARMED;responses[1].payload=second;responses[1].payload_length=(uint16_t)sn;
        } else {
            if (p.protocol.state!=HIL_STATE_ARMED) { nack(f,HIL_NACK_INVALID_STATE); return; }
            if (!hil_plant_model_init(&p.model,&p.config) || !hil_plant_temperature_to_dac(p.config.initial_temp_mC,&p.dac_code) ||
                mcp4725_write_verify(&hi2c1,p.dac_code)!=HAL_OK) { fault_now(2U,(uint16_t)HAL_I2C_GetError(&hi2c1),now);return; }
            p.heater_latched=(uint8_t)(HAL_GPIO_ReadPin(HEATER_CMD_GPIO_Port,HEATER_CMD_Pin)==GPIO_PIN_SET);
            p.sample_index=0U;p.next_telemetry=p.config.telemetry_period_ms;p.run_start=now;p.step_pending=0U;p.step_overlap=0U;p.step_running=0U;
            __HAL_TIM_SET_COUNTER(&htim2,0U);__HAL_TIM_CLEAR_IT(&htim2,TIM_IT_UPDATE);
            if (HAL_TIM_Base_Start_IT(&htim2)!=HAL_OK) { fault_now(6U,2U,now);return; }
            p.timer_running=1U;p.protocol.state=HIL_STATE_RUNNING;sync_status();
            if (!ack_payload(f,a,&an)) return;
        }
    } else if (f->message_type==HIL_MSG_STOP_TEST) {
        hil_stop_test_t stop;hil_test_complete_t done;
        if (f->payload_length!=6U) { nack(f,HIL_NACK_INVALID_PAYLOAD_LENGTH);return; }
        if (f->payload[0]!=HIL_ROLE_PLANT) { nack(f,HIL_NACK_WRONG_TARGET_ROLE);return; }
        if (hil_decode_stop_test(f->payload,f->payload_length,&stop)!=HIL_CODEC_OK) { nack(f,HIL_NACK_INVALID_PAYLOAD_LENGTH);return; }
        if (p.protocol.state!=HIL_STATE_RUNNING) { nack(f,HIL_NACK_INVALID_STATE);return; }
        if (stop.test_id!=p.config.test_id) { nack(f,HIL_NACK_TEST_ID_MISMATCH);return; }
        if (HAL_TIM_Base_Stop_IT(&htim2)!=HAL_OK) { fault_now(6U,3U,now);return; }
        p.timer_running=0U;p.step_pending=0U;HAL_GPIO_WritePin(PLANT_VALID_GPIO_Port,PLANT_VALID_Pin,GPIO_PIN_RESET);p.protocol.state=HIL_STATE_COMPLETE;sync_status();
        done.role=HIL_ROLE_PLANT;done.test_id=stop.test_id;done.outcome=stop.stop_reason?HIL_COMPLETE_HOST_ABORT:HIL_COMPLETE_NORMAL;done.elapsed_ms=now-p.run_start;
        if (hil_encode_test_complete(&done,second,sizeof(second),&sn)!=HIL_CODEC_OK || !ack_payload(f,a,&an)) return;
        count=2U;responses[1].message_type=HIL_MSG_TEST_COMPLETE;responses[1].payload=second;responses[1].payload_length=(uint16_t)sn;
    } else { nack(f,HIL_NACK_UNSUPPORTED_MESSAGE);return; }
    responses[0].message_type=HIL_MSG_ACK;responses[0].payload=a;responses[0].payload_length=(uint16_t)an;
    if (hil_command_cache_commit(&p.commands,f->sequence,f->message_type,f->payload,f->payload_length,responses,count)==HIL_CODEC_OK)
        for (uint8_t i=0U;i<count;++i) respond(responses[i].message_type,responses[i].payload,responses[i].payload_length);
}

static void on_frame(void *context, const hil_transport_frame_t *frame, uint32_t now) {
    (void)context;
    command(frame, now);
}

static void heater_test(void) {
    GPIO_PinState v;
    if(p.protocol.state!=HIL_STATE_READY||p.heater_stage>=3U)return;
    v=HAL_GPIO_ReadPin(HEATER_CMD_GPIO_Port,HEATER_CMD_Pin);
    if(p.heater_stage==1U&&v==GPIO_PIN_SET)p.heater_stage=2U;
    else if(p.heater_stage==2U&&v==GPIO_PIN_RESET)p.heater_stage=3U;
}
static void telemetry(uint32_t now) {
    hil_plant_telemetry_t t;uint8_t b[HIL_PLANT_TELEMETRY_SIZE];size_t n=0U;
    t.role=HIL_ROLE_PLANT;t.test_id=p.config.test_id;t.sample_index=p.sample_index++;
    t.uptime_ms=now;t.simulated_temp_mC=(int32_t)(p.model.temperature_c*1000.0+(p.model.temperature_c>=0?0.5:-0.5));
    t.ambient_temp_mC=(int32_t)(p.model.ambient_c*1000.0+(p.model.ambient_c>=0?0.5:-0.5));
    t.heater_input=p.heater_latched;t.dac_code=p.dac_code;
    t.plant_valid=(uint8_t)(HAL_GPIO_ReadPin(PLANT_VALID_GPIO_Port,PLANT_VALID_Pin)==GPIO_PIN_SET);
    if(hil_encode_plant_telemetry(&t,b,sizeof(b),&n)!=HIL_CODEC_OK)++p.telemetry_drops;
    else (void)send(HIL_MSG_TELEMETRY,b,(uint16_t)n,HIL_TRANSPORT_TELEMETRY);
}
static void model_step(uint32_t now) {
    int32_t temp;uint16_t code;uint8_t heater;
    if(p.step_overlap){p.step_overlap=0U;fault_now(5U,1U,now);return;}
    if(!p.step_pending||p.protocol.state!=HIL_STATE_RUNNING)return;
    p.step_pending=0U;p.step_running=1U;
    if(!hil_plant_model_step(&p.model,p.heater_latched,&temp)){p.step_running=0U;fault_now(6U,4U,now);return;}
    heater=(uint8_t)(HAL_GPIO_ReadPin(HEATER_CMD_GPIO_Port,HEATER_CMD_Pin)==GPIO_PIN_SET);
    if(!hil_plant_temperature_to_dac(temp,&code)){p.step_running=0U;fault_now(6U,4U,now);return;}
    if(mcp4725_write_verify(&hi2c1,code)!=HAL_OK){p.step_running=0U;fault_now(2U,(uint16_t)HAL_I2C_GetError(&hi2c1),now);return;}
    p.dac_code=code;p.heater_latched=heater;
    if(p.model.elapsed_ms>=p.next_telemetry){
        uint32_t period=p.config.telemetry_period_ms;
        uint32_t due=(p.model.elapsed_ms-p.next_telemetry)/period+1U;
        uint64_t next=(uint64_t)p.next_telemetry+(uint64_t)due*period;
        if(due>1U)p.sample_index+=due-1U;
        p.next_telemetry=next>UINT32_MAX?UINT32_MAX:(uint32_t)next;
        telemetry(now);
    }
    p.step_running=0U;
    if(p.step_overlap){p.step_overlap=0U;fault_now(5U,1U,now);}
}

uint8_t plant_runtime_init(void) {
    memset(&p,0,sizeof(p));
    if(hil_protocol_app_init(&p.protocol,plant_transport_instance(),HIL_ROLE_PLANT,(uint16_t)CAPABILITIES,HAL_GetTick())!=HIL_CODEC_OK ||
       hil_command_cache_init(&p.commands)!=HIL_CODEC_OK)return 0U;
    p.protocol.state=HIL_STATE_WAIT_CONFIG;p.protocol.capabilities=(uint16_t)CAPABILITIES;
    hil_protocol_app_set_firmware_version(&p.protocol,0U,6U,0U);
    hil_protocol_app_set_frame_handler(&p.protocol,on_frame,&p);
    sync_status();
    HAL_GPIO_WritePin(PLANT_VALID_GPIO_Port,PLANT_VALID_Pin,GPIO_PIN_RESET);
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0U;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    return 1U;
}
void plant_runtime_service(void) {
    uint32_t now=HAL_GetTick(),start=DWT->CYCCNT,elapsed;
    sync_status();
    hil_protocol_app_service(&p.protocol,now);
    if(p.protocol.boot_enqueue_failures||p.protocol.status_enqueue_failures||
       p.protocol.heartbeat_queue_full_events)p.tx_fault=1U;
    heater_test();model_step(now);
    if(p.protocol.state==HIL_STATE_RUNNING&&(uint32_t)(now-p.run_start)>=p.config.max_runtime_ms)fault_now(7U,0U,now);
    elapsed=DWT->CYCCNT-start;if(elapsed>p.max_cycles)p.max_cycles=elapsed;
    if(SystemCoreClock&&elapsed>SystemCoreClock/(1000U/TASK_BUDGET_MS)){++p.over_budget;fault_now(5U,2U,now);}
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer) {
    if(timer->Instance==TIM2){if(p.step_pending||p.step_running)p.step_overlap=1U;else p.step_pending=1U;}
}
const hil_protocol_app_t *plant_runtime_diagnostics(void){return &p.protocol;}
uint32_t plant_runtime_max_loop_cycles(void){return p.max_cycles;}
uint32_t plant_runtime_over_budget_count(void){return p.over_budget;}
