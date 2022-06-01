#include "gjk.h"

void simplexInit(struct Simplex* simplex) {
    simplex->nPoints = 0;
}

struct Vector3* simplexAddPoint(struct Simplex* simplex, struct Vector3* aPoint, struct Vector3* bPoint) {
    if (simplex->nPoints == MAX_SIMPLEX_SIZE) {
        // SHOULD never happen, but just in case
        return 0;
    }

    int index = simplex->nPoints;

    simplex->objectAPoint[index] = *aPoint;
    vector3Sub(&simplex->objectAPoint[index], bPoint, &simplex->points[index]);
    ++simplex->nPoints;

    return &simplex->points[index];
}

int simplexCheck(struct Simplex* simplex, struct Vector3* nextDirection) {
    struct Vector3* lastAdded = &simplex->points[simplex->nPoints - 1];
    struct Vector3 aToOrigin;
    vector3Negate(lastAdded, &aToOrigin);

    if (simplex->nPoints == 2) {
        struct Vector3 lastAddedToOther;
        vector3Sub(&simplex->points[0], lastAdded, &lastAddedToOther);
        vector3TripleProduct(&lastAddedToOther, &aToOrigin, &lastAddedToOther, nextDirection);

        if (vector3MagSqrd(nextDirection) <= 0.0000001f) {
            vector3Perp(&lastAddedToOther, nextDirection);
        }

        return 0;
    } else if (simplex->nPoints == 3) {
        struct Vector3 normal;
        struct Vector3 ab;
        vector3Sub(&simplex->points[1], lastAdded, &ab);
        struct Vector3 ac;
        vector3Sub(&simplex->points[0], lastAdded, &ac);

        vector3Cross(&ab, &ac, &normal);

        struct Vector3 dirCheck;
        vector3Cross(&ab, &normal, &dirCheck);

        if (vector3Dot(&dirCheck, &aToOrigin) > 0.0f) {
            vector3TripleProduct(&ab, &aToOrigin, &ab, nextDirection);

            if (vector3MagSqrd(nextDirection) <= 0.0000001f) {
                *nextDirection = normal;
            }

            // remove c
            simplex->points[0] = simplex->points[1];
            simplex->points[1] = simplex->points[2];
            simplex->nPoints = 2;

            return 0;
        }

        vector3Cross(&normal, &ac, &dirCheck);

        if (vector3Dot(&dirCheck, &aToOrigin) > 0.0f) {
            vector3TripleProduct(&ac, &aToOrigin, &ac, nextDirection);

            if (vector3MagSqrd(nextDirection) <= 0.0000001f) {
                *nextDirection = normal;
            }

            // remove b
            simplex->points[1] = simplex->points[2];
            simplex->nPoints = 2;

            return 0;
        }

        if (vector3Dot(&normal, &aToOrigin) > 0.0f) {
            *nextDirection = normal;
            return 0;
        }

        // change triangle winding
        struct Vector3 tmp = simplex->points[0];
        simplex->points[0] = simplex->points[1];
        simplex->points[1] = tmp;
        vector3Negate(&normal, nextDirection);
    } else if (simplex->nPoints == 4) {
        int lastBehindIndex = -1;
        int lastInFrontIndex = -1;
        int isFrontCount = 0;

        struct Vector3 normals[3];

        for (int i = 0; i < 3; ++i) {
            struct Vector3 firstEdge;
            struct Vector3 secondEdge;
            vector3Sub(lastAdded, &simplex->points[i], &firstEdge);
            vector3Sub(i == 2 ? &simplex->points[0] : &simplex->points[i + 1], &simplex->points[i], &secondEdge);
            vector3Cross(&firstEdge, &secondEdge, &normals[i]);

            if (vector3Dot(&aToOrigin, &normals[i]) > 0.0f) {
                ++isFrontCount;
                lastInFrontIndex = i;
            } else {
                lastBehindIndex = i;
            }
        }

        if (isFrontCount == 0) {
            // enclosed the origin
            return 1;
        } else if (isFrontCount == 1) {
            *nextDirection = normals[lastInFrontIndex];

            if (lastInFrontIndex == 1) {
                simplex->points[0] = simplex->points[1];
                simplex->points[1] = simplex->points[2];
            } else if (lastInFrontIndex == 2) {
                simplex->points[1] = simplex->points[0];
                simplex->points[0] = simplex->points[2];
            }

            simplex->points[2] = simplex->points[3];
            simplex->nPoints = 3;
        } else if (isFrontCount == 2) {
            if (lastBehindIndex == 0) {
                simplex->points[0] = simplex->points[2];
            } else if (lastBehindIndex == 2) {
                simplex->points[0] = simplex->points[1];
            }

            simplex->points[1] = simplex->points[3];
            simplex->nPoints = 2;

            struct Vector3 ab;
            vector3Sub(&simplex->points[0], &simplex->points[1], &ab);

            vector3TripleProduct(&ab, &aToOrigin, &ab, nextDirection);

            if (vector3MagSqrd(nextDirection) <= 0.0000001f) {
                vector3Perp(&ab, nextDirection);
            }
        } else {
            // this case shouldn't happen but if it does
            // this is the correct logic
            simplex->points[0] = simplex->points[3];
            simplex->nPoints = 1;
            *nextDirection = aToOrigin;
        }
    }

    return 0;
}

#define MAX_GJK_ITERATIONS  10

int gjkCheckForOverlap(struct Simplex* simplex, void* objectA, MinkowsiSum objectASum, void* objectB, MinkowsiSum objectBSum, struct Vector3* firstDirection) {
    struct Vector3 aPoint;
    struct Vector3 bPoint;
    struct Vector3 nextDirection;

    simplexInit(simplex);

    if (vector3IsZero(firstDirection)) {
        objectASum(objectA, &gRight, &aPoint);
        vector3Negate(&gRight, &nextDirection);

        objectBSum(objectB, &nextDirection, &bPoint);
        simplexAddPoint(simplex, &aPoint, &bPoint);
    } else {
        objectASum(objectA, firstDirection, &aPoint);
        vector3Negate(firstDirection, &nextDirection);

        objectBSum(objectB, &nextDirection, &bPoint);
        simplexAddPoint(simplex, &aPoint, &bPoint);
    }

    for (int iteration = 0; iteration < MAX_GJK_ITERATIONS; ++iteration) {
        struct Vector3 reverseDirection;
        vector3Negate(&nextDirection, &reverseDirection);
        objectASum(objectA, &nextDirection, &aPoint);
        objectBSum(objectB, &reverseDirection, &bPoint);

        struct Vector3* addedPoint = simplexAddPoint(simplex, &aPoint, &bPoint);

        if (!addedPoint) {
            return 0;
        }
        
        if (vector3Dot(addedPoint, &nextDirection) <= 0.0f) {
            return 0;
        }


        if (simplexCheck(simplex, &nextDirection)) {
            return 1;
        }

    }

    return 0;
}