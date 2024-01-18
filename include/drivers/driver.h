#ifndef __GTOS__DRIVERS__DRIVER_H
#define __GTOS__DRIVERS__DRIVER_H

namespace gtos {
    namespace drivers {
        class Driver {
            public:
                Driver();
                ~Driver();

                virtual void Activate();
                virtual int Reset();
                virtual void Deactivate();
        };

        class DriverManager {
            private:
                
                int numDrivers;
            public:
                Driver* drivers[255];
                DriverManager();
                void AddDriver(Driver*);
                void ActivateAll();
        };

    }
}


#endif