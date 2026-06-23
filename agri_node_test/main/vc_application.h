/**
 ******************************************************************************
 * @file    vc_application.h
 * @brief   Application logic and FSM for Agriculture Node
 ******************************************************************************
 */

#ifndef VC_APPLICATION_H
#define VC_APPLICATION_H

/**
 * @brief Initializes hardware and starts the main application FSM loop.
 * This function contains an infinite loop and will not return.
 */
void vc_application_start(void);

#endif /* VC_APPLICATION_H */