// ========================= config =========================

#include <iostream>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <optional>
#include <string>
#include <algorithm>
#include <vector>



#define GL_SILENCE_DEPRECATION
#include <GLUT/glut.h>

#define _USE_MATH_DEFINES
#include <cmath>

#define SEED 2
#define TWO_PI 6.28318530718

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define VIEW_DISTANCE 450
#define ROTATION_SCALE 0.6f

#define KILL_R_START 10
#define SPAWN_R_START 6

#define MAX_NC 10000

// multithreading safety
std::mutex particleMutex;
std::atomic<bool> goingFast(false);
std::atomic<bool> saving(true);

std::mutex renderMutex;

struct Vector3i {
	int x, y, z;

	Vector3i(int x = 0, int y = 0, int z = 0) : x(x), y(y), z(z) {}

	bool operator==(const Vector3i& other) const {
		return (x == other.x && y == other.y && z == other.z);
	}

	bool operator!=(const Vector3i& other) const {
        return !(*this == other);
    }
};

std::vector<Vector3i> renderCopy;


namespace std {
    template <>

    // Szudzik pairing function
    struct hash<Vector3i> {
        size_t operator()(const Vector3i& v) const {
            return v.x + 73856093 * v.y + 19349663 * v.z;
        }
    };
}

// GLOBAL DECLARATIONS

// system objects
Vector3i position(0,0,0); // this is the walker
Vector3i lastPos(0,0,0);
std::unordered_set<Vector3i> occupiedPositions;
std::unordered_set<Vector3i> adjacencies; // major performance optimisation. RAM isn't an issue at this scale.
uint32_t state = SEED;
uint32_t state2 = SEED;

bool in_3D = true;

const uint32_t seeds[] = {4000};//{1,2,3,4,5,6,7,8,9,10};
const float stickProbs[] = {0.2};//{1,0.8,0.6,0.4,0.2};

float stickProb;

// system 'constants'
int killRadius = KILL_R_START;
int killRadiusSquared = killRadius * killRadius;
int spawnRadius = SPAWN_R_START;
float fractal_size = 0;
float particleScale = WINDOW_WIDTH / killRadius / 2;
float halfScale = particleScale / 2.0f;

// inputs
float rotationX = 0.0f;
float rotationY = 0.0f;
int lastMouseX = 0;
int lastMouseY = 0;
bool mouseDown = false;


// analytics
auto start = std::chrono::high_resolution_clock::now();
float nextNum = 1.0f;
int stuck = 0;


// ==========================================================





// ================= rng tools container =====================

namespace RandomUtils {

    inline void xorShift() { // much faster PRNG. Period and security are not a problem in this context.
	    state ^= state << 13;
	    state ^= state >> 17;
	    state ^= state << 5;
	}

	inline void xorShift2() { // much faster PRNG. Period and security are not a problem in this context.
	    state2 ^= state2 << 13;
	    state2 ^= state2 >> 17;
	    state2 ^= state2 << 5;
	}

	int DegreeOfAdjacency() {
		int count = 0;

		if (occupiedPositions.find(Vector3i(position.x+1,position.y,position.z)) != occupiedPositions.end()) count++;
		if (occupiedPositions.find(Vector3i(position.x-1,position.y,position.z)) != occupiedPositions.end()) count++;
		if (occupiedPositions.find(Vector3i(position.x,position.y+1,position.z)) != occupiedPositions.end()) count++;
		if (occupiedPositions.find(Vector3i(position.x,position.y-1,position.z)) != occupiedPositions.end()) count++;
		if (occupiedPositions.find(Vector3i(position.x,position.y,position.z+1)) != occupiedPositions.end()) count++;
		if (occupiedPositions.find(Vector3i(position.x,position.y,position.z-1)) != occupiedPositions.end()) count++;

		return count;
	}

	inline bool DidItStick() {
		// xorShift2();

		// int adjs = DegreeOfAdjacency();

		// if (adjs == 1) {return state < 0.01 * UINT32_MAX;}
		// if (adjs == 2) {return state < 0.5 * UINT32_MAX;}
		return true;

	}

    inline bool KilledAfterRandomMove() {
    	lastPos.x = position.x; lastPos.y = position.y; lastPos.z = position.z;
	    std::lock_guard<std::mutex> lock(particleMutex);
	    xorShift(); position.x += (state % 3) - 1;
	    xorShift(); position.y += (state % 3) - 1;
	    if (in_3D) {
	        xorShift();
	        position.z += (state % 3) - 1;
	    }
    	return (position.x * position.x + position.y * position.y + position.z * position.z > killRadiusSquared);
}

    void Spawn() {

    	float z = 0;

    	xorShift();

		if (in_3D) {
			z = 2.0f * ((state & 0x7FFFFFFF) / float(0x7FFFFFFF)) - 1.0f; // uniform z in [-1,1]
		}

		float r = sqrt(1.0f - z * z) * spawnRadius;

		xorShift();
		float t = TWO_PI * (state & 0x7FFFFFFF) / float(0x7FFFFFFF);

		position.x = r * cos(t);
		position.y = r * sin(t);
		position.z = z * spawnRadius;


    }
}

// ==========================================================





// ============-== system tools container ===================

namespace SystemUtils {

	void InsertAdjacencies() {
		int X = position.x;
		int Y = position.y;
		int Z = position.z;

		adjacencies.insert(Vector3i(X,Y+1,Z));
		adjacencies.insert(Vector3i(X+1,Y,Z));
		adjacencies.insert(Vector3i(X-1,Y,Z));
		adjacencies.insert(Vector3i(X,Y-1,Z));

		if (in_3D) {
			adjacencies.insert(Vector3i(X,Y,Z+1));
			adjacencies.insert(Vector3i(X,Y,Z-1));
		}
	}

	// CASES: (not adjacent, not overlapping), (adjacent, overlapping), (adjacent, not stuck), (adjacent, stuck)

	inline void Diffuse() {

		bool particleStuck = false;

		while (!particleStuck) {
		    if (adjacencies.find(position) == adjacencies.end()) {
		        if (RandomUtils::KilledAfterRandomMove()) {
		            RandomUtils::Spawn();
		        }
		    } else if (occupiedPositions.find(position) != occupiedPositions.end()) {
		    	std::lock_guard<std::mutex> lock(particleMutex);
		    	position.x = lastPos.x; position.y = lastPos.y; position.z = lastPos.z;
		    } else {
		    	if (RandomUtils::DidItStick()) {
		    		particleStuck = true;
		    	} else {
		    		RandomUtils::KilledAfterRandomMove();
		    	}
		    }
		}
		std::lock_guard<std::mutex> lock(particleMutex);
		stuck++;
	}

}

// ==========================================================





// ================= GL UT render pipeline ==================

namespace WindowUtils {

	void DrawCube(float x, float y, float z) {
	    glBegin(GL_QUADS);

	    // front
	    glNormal3f(0.0f, 0.0f, 1.0f);
	    glVertex3f(x - halfScale, y - halfScale, z + halfScale);
	    glVertex3f(x + halfScale, y - halfScale, z + halfScale);
	    glVertex3f(x + halfScale, y + halfScale, z + halfScale);
	    glVertex3f(x - halfScale, y + halfScale, z + halfScale);

	    // back
	    glNormal3f(0.0f, 0.0f, -1.0f);
	    glVertex3f(x - halfScale, y - halfScale, z - halfScale);
	    glVertex3f(x - halfScale, y + halfScale, z - halfScale);
	    glVertex3f(x + halfScale, y + halfScale, z - halfScale);
	    glVertex3f(x + halfScale, y - halfScale, z - halfScale);

	    // left
	    glNormal3f(-1.0f, 0.0f, 0.0f);
	    glVertex3f(x - halfScale, y - halfScale, z - halfScale);
	    glVertex3f(x - halfScale, y - halfScale, z + halfScale);
	    glVertex3f(x - halfScale, y + halfScale, z + halfScale);
	    glVertex3f(x - halfScale, y + halfScale, z - halfScale);

	    // right
	    glNormal3f(1.0f, 0.0f, 0.0f);
	    glVertex3f(x + halfScale, y - halfScale, z - halfScale);
	    glVertex3f(x + halfScale, y + halfScale, z - halfScale);
	    glVertex3f(x + halfScale, y + halfScale, z + halfScale);
	    glVertex3f(x + halfScale, y - halfScale, z + halfScale);

	    // top
	    glNormal3f(0.0f, 1.0f, 0.0f);
	    glVertex3f(x - halfScale, y + halfScale, z - halfScale);
	    glVertex3f(x - halfScale, y + halfScale, z + halfScale);
	    glVertex3f(x + halfScale, y + halfScale, z + halfScale);
	    glVertex3f(x + halfScale, y + halfScale, z - halfScale);

	    // bottom
	    glNormal3f(0.0f, -1.0f, 0.0f);
	    glVertex3f(x - halfScale, y - halfScale, z - halfScale);
	    glVertex3f(x + halfScale, y - halfScale, z - halfScale);
	    glVertex3f(x + halfScale, y - halfScale, z + halfScale);
	    glVertex3f(x - halfScale, y - halfScale, z + halfScale);

	    glEnd();
	}

	void Viridis(float t, float &r, float &g, float &b) {
	    // viridis colormap; 10 samples
	    const float colors[10][3] = {
	        {0.267, 0.004, 0.329}, // dark bluey purple
	        {0.283, 0.141, 0.458},
	        {0.254, 0.265, 0.530},
	        {0.207, 0.372, 0.553},
	        {0.164, 0.471, 0.558}, // cyan kinda thing
	        {0.128, 0.567, 0.551},
	        {0.128, 0.659, 0.518},
	        {0.227, 0.749, 0.382},
	        {0.424, 0.814, 0.221},
	        {0.706, 0.871, 0.157}  // yellow
	    };

	    t = std::clamp(t, 0.0f, 1.0f);

	    float scaled_t = t * 9.0f;
	    int index = static_cast<int>(scaled_t);

	    // interpolation
	    float alpha = scaled_t - index; 

	    r = (1.0f - alpha) * colors[index][0] + alpha * colors[index + 1][0];
	    g = (1.0f - alpha) * colors[index][1] + alpha * colors[index + 1][1];
	    b = (1.0f - alpha) * colors[index][2] + alpha * colors[index + 1][2];
	}

	void Plasma(float t, float &r, float &g, float &b) {
	    const float colors[10][3] = {
	        {0.050, 0.029, 0.528}, // purple
	        {0.294, 0.002, 0.708},
	        {0.496, 0.027, 0.716},
	        {0.678, 0.089, 0.667},
	        {0.831, 0.216, 0.553}, // pink
	        {0.956, 0.360, 0.404},
	        {0.990, 0.553, 0.242},
	        {0.970, 0.750, 0.095},
	        {0.882, 0.949, 0.020},
	        {0.940, 1.000, 0.000}  // yellow
	    };

	    t = std::clamp(t, 0.0f, 0.99999999f);

	    float scaled_t = t * 9.0f;
	    int idx = static_cast<int>(scaled_t);
	    float alpha = scaled_t - idx;

	    r = (1.0f - alpha) * colors[idx][0] + alpha * colors[idx + 1][0];
	    g = (1.0f - alpha) * colors[idx][1] + alpha * colors[idx + 1][1];
	    b = (1.0f - alpha) * colors[idx][2] + alpha * colors[idx + 1][2];
	}




    void Display() {
	    particleScale = (float)WINDOW_WIDTH / killRadius / 1.3f;
	    
	    std::vector<Vector3i> positionsCopy;
	    {
	        std::lock_guard<std::mutex> lock(particleMutex);
	        positionsCopy = std::vector<Vector3i>(occupiedPositions.begin(), occupiedPositions.end());
	    }

	    if (in_3D) {
	        halfScale = particleScale / 2.0f;
	        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	        glLoadIdentity();
	        glTranslatef(0, 0, -VIEW_DISTANCE);
	        glRotatef(rotationX, 1, 0, 0);
	        glRotatef(rotationY, 0, 1, 0);

	        for (const Vector3i& v : positionsCopy) {
	            float distance = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	            float normalisedDist = distance / fractal_size;

	            float r, g, b;
	            Plasma(normalisedDist, r, g, b);

	            glColor3f(r,g,b);
	            
	            DrawCube(v.x * particleScale, v.y * particleScale, v.z * particleScale);
	        }
	    } else {
	        glPointSize(particleScale);
	        glClear(GL_COLOR_BUFFER_BIT);
	        glBegin(GL_POINTS);

	        for (const Vector3i& v : positionsCopy) {
	            float distance = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	            float normalisedDist = distance / fractal_size;

	            float r, g, b;
	            Viridis(normalisedDist, r, g, b);
	            
	            glVertex3f((float)v.x * particleScale, (float)v.y * particleScale, (float)v.z * particleScale);
	        }
	        glEnd();
	    }
	    std::cout << "\rNumber of particles: " << stuck << "     Fractal size: " << fractal_size << std::flush;

	    glutSwapBuffers();
	}

    void InitOpenGL() {
    	glEnable(GL_MULTISAMPLE);

    	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    	if (in_3D) {
    		glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);

			glClearDepth(1.0f);

		    glEnable(GL_LIGHTING);
		    glEnable(GL_LIGHT0); // default

		    GLfloat lightPos[] = { 300.0f, 600.0f, 300.0f, 1.0f }; // light direction
		    GLfloat lightAmbient[] = { 0.2f, 0.2f, 0.2f, 1.0f };
		    GLfloat lightDiffuse[] = { 0.9f, 0.9f, 0.9f, 1.0f };
		    GLfloat lightSpecular[] = { 1.0f, 1.0f, 1.0f, 1.0f };

		    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
		    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
		    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
		    glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpecular);

		    glEnable(GL_COLOR_MATERIAL);
		    glShadeModel(GL_SMOOTH);
		    
		    glMatrixMode(GL_PROJECTION);
		    glLoadIdentity();

		    // FOV: 60 degrees, aspect ratio: window width/height, near clipping: 1, far clipping: 1000
		    gluPerspective(70.0f, (double)WINDOW_WIDTH / (double)WINDOW_HEIGHT, 1.0f, 1000.0f); 

		    glMatrixMode(GL_MODELVIEW);
		    glLoadIdentity();
		    
		    gluLookAt(0, 0, VIEW_DISTANCE,   // eye position
		              0, 0, 0,     // look-at position (0,0)
		              0, 1, 0);    // y axis (up)

		    glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);

		} else {
			glOrtho(-WINDOW_WIDTH / 2, WINDOW_WIDTH / 2, -WINDOW_HEIGHT / 2, WINDOW_HEIGHT / 2, -1.0f, 1.0f);
		}
    }

    void Update(int value) {
    	(void)value;

	    glutPostRedisplay();
	    glutTimerFunc(20, Update, 0);
	}



	void MousePress(int button, int mouseState, int x, int y) {
        if (button == GLUT_LEFT_BUTTON) {
            if (mouseState == GLUT_DOWN) {
                mouseDown = true;
                lastMouseX = x;
                lastMouseY = y;
            } else if (mouseState == GLUT_UP) {
                mouseDown = false;
            }
        }
    }

    void MouseMove(int x, int y) {
        if (mouseDown) {
            float dx = (x - lastMouseX) * ROTATION_SCALE;
            float dy = (y - lastMouseY) * ROTATION_SCALE;
            
            rotationY += dx;
            rotationX += dy;
            
            lastMouseX = x;
            lastMouseY = y;
            
            glutPostRedisplay();
        }
    }
}


// ==========================================================






// =================== threading tools ======================

	namespace ThreadUtils {

	void FastLoop() {

	    while (goingFast) {

	    	SystemUtils::Diffuse();
	    	SystemUtils::InsertAdjacencies();

	        fractal_size = fmax(fractal_size, sqrt((position.x * position.x) + (position.y * position.y) + (position.z * position.z)));
	        
	        spawnRadius = (fractal_size + 5) * 1.4;
	        killRadius = (fractal_size + 5) * 2;
	        killRadiusSquared = killRadius * killRadius;

	        std::lock_guard<std::mutex> lock(particleMutex);
	    	occupiedPositions.insert(position);

	        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 1kfps limit

	       	RandomUtils::Spawn();

	    }

	}

	void SavingLoop(std::string fileName) {
		std::ofstream outputFile(fileName);
		while (saving) {
			if (stuck >= nextNum) {
				outputFile << stuck << "," << fractal_size << std::endl;
				nextNum *= 1.2;
			}
		}
		outputFile.close();
	}


	void simThreadContainer() {
		for (const uint32_t& s : seeds) {
		    for (const float& p : stickProbs) {
				std::unique_lock<std::mutex> lock(particleMutex);

			    nextNum = 1.0f;
			    killRadius = KILL_R_START;
				killRadiusSquared = killRadius * killRadius;
				spawnRadius = SPAWN_R_START;
				fractal_size = 0;
				particleScale = WINDOW_WIDTH / killRadius / 2;
				halfScale = particleScale / 2.0f;
				
				position.x = 0; position.y = 0; position.z = 0;
				stuck = 0;
				occupiedPositions.clear();
				adjacencies.clear();
				SystemUtils::InsertAdjacencies();
				occupiedPositions.insert(position);

				lock.unlock();

				

				RandomUtils::Spawn();

		    	state = s;
		    	state2 = s + 1234567;
		    	stickProb = p;

				std::string fileName = "data/" + std::to_string(s) + "-" + std::to_string( (int)(stickProb * 100) ) + "per-" + std::to_string(MAX_NC) + ".txt";

				goingFast = true;
				saving = true;


				std::thread physicsThread(ThreadUtils::FastLoop);
				std::thread saveThread(ThreadUtils::SavingLoop,fileName);

			    while (stuck < MAX_NC) {}

			    std::cout << "\n\n" << s << ", " << p << std::endl;

			    saving = false;
			    goingFast = false;
			    saveThread.join();
			    physicsThread.join();

		    }
		}

	}

}
// ==========================================================





// ========================= main ===========================

int main(int argc, char** argv) {

   	occupiedPositions.insert(position);
   	SystemUtils::InsertAdjacencies();

    glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    glutCreateWindow("DLA Simulation");

    glutMouseFunc(WindowUtils::MousePress);
    glutMotionFunc(WindowUtils::MouseMove);

    WindowUtils::InitOpenGL();
    glutTimerFunc(0, WindowUtils::Update, 0);
    glutDisplayFunc(WindowUtils::Display);

    std::thread simThread(ThreadUtils::simThreadContainer);


	glutMainLoop();


    return 0;
}

// ==========================================================

