#ifndef __KART_TAGS_H
#define __KART_TAGS_H

#define TAG(__a,__b,__c,__d) ((__a & 0xFF) << 24) + ((__b & 0xFF) << 16) + ((__c & 0xFF) << 8) + (__d & 0xFF)
#define KART_TAG TAG('K','A','R','T')
#define FLYABLE_TAG TAG('F','L','Y','!')
#define NO_COLLISION_KART_TAG TAG('G','H','O','S')
#define GHOST_NO_COLLECTIBLE_KART_TAG TAG('G','H','N','C')

#endif
