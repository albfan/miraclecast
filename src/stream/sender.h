/*
 * =====================================================================================
 *
 *       Filename:  sender.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2016年11月25日 10時42分20秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#define MIRACLE_SENDER_ERROR 		sender_impl_error_quark()

enum {
	MIRACLE_SENDER_ERROR_UNKNOWN,
	MIRACLE_SENDER_ERROR_NOT_PREPARED,
	MIRACLE_SENDER_ERROR_AGAIN,
};
