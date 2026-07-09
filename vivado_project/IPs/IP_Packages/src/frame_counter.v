`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 04/12/2026 08:54:10 PM
// Design Name: 
// Module Name: frame_counter
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module frame_counter
#(

parameter clk_freq=200_000_000,
parameter vertical_count=720
)
(
input clk,
input resetn,
output reg [31:0] count_frame,
output reg [31:0] count_frame_old,
output reg [31:0] count_frame_total,

input wire [31:0]v_axis_tdata,
input wire v_axis_tvalid,
input wire v_axis_tready,
input wire v_axis_tlast
    );
reg  [31:0] count_vert;
reg  [31:0] counter;
    
always@(posedge clk)
    begin
    if(!resetn)begin
        count_frame <= 0;
        counter <= 0;
        count_vert <= 0;
        count_frame_old <=0;
        count_frame_total <=0;
    end else begin
        if(counter == (clk_freq - 1))begin
            count_frame <= 0;
            counter <= 0;
            count_vert <= 0;
            count_frame_old <=count_frame;
        end else begin
            counter <= counter + 1;
            if(v_axis_tvalid && v_axis_tlast && v_axis_tready)begin
                if(count_vert == (vertical_count - 1))begin
                    count_vert <= 0;
                    count_frame_total <= count_frame_total + 1;
                    count_frame <= count_frame + 1;
                end else begin
                    count_vert <= count_vert+1;
                end
                    
            end
        end
    end
end
endmodule
