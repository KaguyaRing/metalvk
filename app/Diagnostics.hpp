// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#pragma once
#import <Foundation/Foundation.h>
@interface MetalVKReport : NSObject
@property(nonatomic, copy) NSString *text;
@property(nonatomic) NSUInteger devices;
@property(nonatomic) BOOL failed;
@end
MetalVKReport *MetalVKReadDiagnostics(void);
