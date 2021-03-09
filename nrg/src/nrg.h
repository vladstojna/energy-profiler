/* nrg.h */

#pragma once

/* Handles */

typedef struct nrg_task_st* nrg_task_t;

typedef struct nrg_exec_st* nrg_exec_t;

typedef struct nrg_sample_st* nrg_sample_t;

typedef struct nrg_reader_cpu_st* nrg_reader_cpu_t;

typedef struct nrg_reader_gpu_st* nrg_reader_gpu_t;

typedef struct nrg_event_cpu_st* nrg_event_cpu_t;

typedef struct nrg_event_gpu_st* nrg_event_gpu_t;

/* Enums */

typedef enum nrg_events_et
{
    NRG_MAX_DEVS = 8,
    NRG_MAX_SKTS = 8
} nrg_events_t;

typedef enum nrg_error_code_et
{
    NRG_SUCCESS = 0,
    NRG_ENOTIMPL,
    NRG_ESYS,
    NRG_EREAD,
    NRG_ESETUP,
    NRG_ENOEV,
    NRG_EOOB,
    NRG_EUNKNOWN,
    NRG_EBADALLOC
} nrg_error_code_t;

/* Structs */

enum nrg_msg_len_t
{
    NRG_ERROR_MSG_LEN = 128
};

typedef struct nrg_return_t
{
    nrg_error_code_t code;
    char msg[NRG_ERROR_MSG_LEN];
} nrg_error_t;

/* Functions */

nrg_error_t nrgTask_create(nrg_task_t*, unsigned int);
void nrgTask_destroy(nrg_task_t);

unsigned int nrgTask_id(const nrg_task_t);
unsigned int nrgTask_exec_count(const nrg_task_t);

nrg_exec_t nrgTask_exec_by_idx(const nrg_task_t, unsigned int);

nrg_error_t nrgTask_add_exec(nrg_task_t, unsigned int*);
nrg_error_t nrgTask_add_execs(nrg_task_t, unsigned int*, unsigned int);
nrg_error_t nrgTask_reserve(nrg_task_t, unsigned int);

/* ------------------------------------------------------------------------- */

/* TODO */
nrg_error_t nrgExec_create(nrg_exec_t*, unsigned int);
void nrgExec_destroy(nrg_exec_t);

unsigned int nrgExec_id(const nrg_exec_t);
unsigned int nrgExec_sample_count(const nrg_exec_t);

nrg_sample_t nrgExec_sample_by_idx(const nrg_exec_t, unsigned int);
nrg_sample_t nrgExec_first_sample(const nrg_exec_t);
nrg_sample_t nrgExec_last_sample(const nrg_exec_t);

nrg_error_t nrgExec_reserve(nrg_exec_t, unsigned int);
nrg_error_t nrgExec_add_sample(nrg_exec_t, long long, unsigned int*);

/* ------------------------------------------------------------------------- */

/* TODO */
nrg_error_t nrgSample_create(nrg_sample_t*, long long);
void nrgSample_destroy(nrg_sample_t);

long long nrgSample_timepoint(const nrg_sample_t);
long long nrgSample_duration(const nrg_sample_t, const nrg_sample_t);
void nrgSample_update(nrg_sample_t, long long);

/* ------------------------------------------------------------------------- */

nrg_error_t nrgReader_create_cpu(nrg_reader_cpu_t*, unsigned char, unsigned char);
nrg_error_t nrgReader_create_gpu(nrg_reader_gpu_t*, unsigned char);
void nrgReader_destroy_cpu(nrg_reader_cpu_t);
void nrgReader_destroy_gpu(nrg_reader_gpu_t);

nrg_error_t nrgReader_read_cpu(const nrg_reader_cpu_t, nrg_sample_t);
nrg_error_t nrgReader_read_gpu(const nrg_reader_gpu_t, nrg_sample_t);

nrg_event_cpu_t nrgReader_event_cpu(const nrg_reader_cpu_t, unsigned char);
nrg_event_gpu_t nrgReader_event_gpu(const nrg_reader_gpu_t, unsigned char);

/* ------------------------------------------------------------------------- */

nrg_error_t nrgEvent_pkg(const nrg_event_cpu_t, const nrg_sample_t, long long*);
nrg_error_t nrgEvent_pp0(const nrg_event_cpu_t, const nrg_sample_t, long long*);
nrg_error_t nrgEvent_pp1(const nrg_event_cpu_t, const nrg_sample_t, long long*);
nrg_error_t nrgEvent_dram(const nrg_event_cpu_t, const nrg_sample_t, long long*);
nrg_error_t nrgEvent_board_pwr(const nrg_event_gpu_t, const nrg_sample_t, long long*);

/* Other functions */

int nrg_is_error(const nrg_error_t* err);
