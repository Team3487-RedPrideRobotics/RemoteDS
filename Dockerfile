FROM arm32v7/node:slim
#RUN git clone https://github.com/Team3487-RedPrideRobotics/RemoteDS.git
COPY . /RemoteDS
WORKDIR /RemoteDS
EXPOSE 1110/udp
EXPOSE 1150/udp
CMD ["node RemoteDS"] 
