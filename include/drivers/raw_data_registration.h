#ifndef __GTOS__DRIVERS__RAW_DATA_REGISTRATION_H
#define __GTOS__DRIVERS__RAW_DATA_REGISTRATION_H

#include <common/types.h>

namespace gtos {
    namespace drivers {
        class RawDataHandler;
        class RawDataRegistrationCell;

        // A non-owning identity. Issued is not a claim that the binding is live.
        class RawDataRegistrationKey {
        public:
            RawDataRegistrationKey() : issuer(0), target(0), generation(0) {}
            RawDataRegistrationKey(const RawDataRegistrationKey&) = default;
            RawDataRegistrationKey& operator=(const RawDataRegistrationKey&) = default;
            bool Issued() const { return issuer != 0 && target != 0 && generation != 0; }
        private:
            friend class RawDataRegistrationCell;
            const RawDataRegistrationCell* issuer;
            RawDataHandler* target;
            uint32_t generation;
        };

        // One accepted use. The permanent cell outlives this stack record.
        class RawDataRegistrationLease {
        public:
            RawDataRegistrationLease() : issuer(0), target(0), generation(0), held(false) {}
            ~RawDataRegistrationLease();
            bool Held() const { return held; }
            RawDataHandler* CapturedTarget() const { return held ? target : 0; }
        private:
            friend class RawDataRegistrationCell;
            RawDataRegistrationCell* issuer;
            RawDataHandler* target;
            uint32_t generation;
            bool held;
            RawDataRegistrationLease(const RawDataRegistrationLease&);
            RawDataRegistrationLease& operator=(const RawDataRegistrationLease&);
        };

        // An isolated typed primitive, not a driver or IRQ registration replacement.
        // It never destroys a handler. Every registration must drain before destruction.
        // A drained key says nothing about other bindings or the target still being alive.
        class RawDataRegistrationCell {
        public:
            static RawDataRegistrationCell& BootCell();
            bool TryBind(RawDataHandler* target, RawDataRegistrationKey* out);
            bool Acquire(RawDataRegistrationLease* out);
            bool RequestRetire(const RawDataRegistrationKey& key);
            bool Release(RawDataRegistrationLease* lease);
            // A query for this key only. False never waits or changes state.
            bool TryDrain(const RawDataRegistrationKey& key) const;
        private:
#ifdef GTOS_RAW_DATA_REGISTRATION_TEST
            friend class RawDataRegistrationCellTestAccess;
#endif
            enum State { CELL_FREE, CELL_BOUND, CELL_RETIRING, CELL_EXHAUSTED };
            constexpr RawDataRegistrationCell()
                : target(0), generation(0), active(0), state(CELL_FREE) {}
            ~RawDataRegistrationCell() = default;
            RawDataRegistrationCell(const RawDataRegistrationCell&);
            RawDataRegistrationCell& operator=(const RawDataRegistrationCell&);
            void FinishRetirementLocked();
            static RawDataRegistrationCell bootCell;
            RawDataHandler* target;
            uint32_t generation;
            uint32_t active;
            State state;
        };
    }
}

#endif
