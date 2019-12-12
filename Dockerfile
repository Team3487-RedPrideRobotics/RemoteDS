FROM arm64v8/gcc
#RUN git clone https://github.com/Team3487-RedPrideRobotics/RemoteDS.git
COPY . /RemoteDS
WORKDIR /RemoteDS
RUN g++ -o RemoteDS test.cpp
CMD ["./RemoteDS"] 